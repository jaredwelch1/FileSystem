#include "../include/block_store.h"

// Overriding these will probably break it since I'm not testing it that much
// It probably won't go crazy so long as the sizes are reasonable and powers of two
// Touching these will void your warranty
#ifndef BLOCK_COUNT
    #define BLOCK_COUNT 65536
#endif
#ifndef BLOCK_SIZE
    #define BLOCK_SIZE 1024
#endif
#define FBM_BLOCK_COUNT ((BLOCK_COUNT >> 3) / BLOCK_SIZE)
#define DATA_BLOCK_COUNT (BLOCK_COUNT - FBM_BLOCK_COUNT)
#if (((FBM_BLOCK_COUNT * BLOCK_SIZE) << 3) != BLOCK_COUNT)
    // If this doesn't come out right, truncation happened somewhere
    #error "BLOCK MATH DIDN'T CHECK OUT"
#endif

// Handy macros, does what it says on the tin
#define BLOCKID_VALID(id) (((id) >= (FBM_BLOCK_COUNT)) && ((id) < BLOCK_COUNT))
#define BLOCK_POSITION(id) ((id) * BLOCK_SIZE)
#define BLOCK_OFFSET_POSITION(id, offset) (((id) * BLOCK_SIZE) + (offset))
// Because I couldn't think of a good name for this
// When the FBM state changes for the block, id, this calculates the FBM
// Block that was changed so it can be marked in the DBM
#define FBM_BLOCK_CHANGE_LOCATION(id) (((id) >> 3) / BLOCK_SIZE)


// FUTURE NOTE: capture st_blksize from the stat for optimal file I/O
// (use of st_blksize requires _XOPEN_SOURCE >= 500)
// and pass it to the utilities
// this will (hopefully) prevent the horror of read-modify-writes
// the preferred block size can vary from file to file, so we can't really hardcode it
// (but 4K is probably a good guess)
// (but flushing will do 1K writes... Ugh)

size_t utility_read_file(const int fd, uint8_t *buffer, const size_t count);

size_t utility_write_file(const int fd, const uint8_t *buffer, const size_t count);

// The errno. Accessed by the block_store_errno function
bs_status bs_errno = BS_OK;

// Flags, yay!
// Most won't be used (yet)
// make sure ALL is as wide as the largest flag
typedef enum {NONE = 0x00, FILE_LINKED = 0x01, FILE_BASED = 0x02, DIRTY = 0x04, ALL = 0xFF} BS_FLAGS;

struct block_store {
    int fd; // R/W position never guarenteed, flag indicates link state (attempt to set it to -1 when not in use as well)
    BS_FLAGS flags;
    bitmap_t *dbm;
    bitmap_t *fbm;
    uint8_t *data_blocks;
};
// Idea, claim block 8 for "utility" purposes
//  (or, more accurately, block FBM_BLOCK_COUNT)
// Could use it to store a hash of the full object (set hash to all 0 on hash of that region)
// That way we can optionally disable the DBM format on link
// Or is that an FS-level thing?
// Maybe we should have kept it file-backed.


// Tiny struct to store a bs obj and a byte_counter for tracking total read/write
typedef struct {
    int disaster_errno;
    block_store_t *const bs;
    size_t byte_counter;
    bs_status status;
} bs_sync_obj;

void block_sync(size_t block_id, void *bs_ptr);



#define FLAG_CHECK(block_store, flag) (block_store->flags & flag)
// putting this in parens looks weird but it feels unsafe not doing it
#define FLAG_CLEAR(block_store, flag) block_store->flags &= ~flag
#define FLAG_SET(block_store, flag) block_store->flags |= flag


block_store_t *block_store_create() {
    // While assembly-wise it shouldn't change, it looks cleaner,
    //   even if we have extra free/destruct calls on the error path
    //   (but then again, who cares about the length of the error path?)

    // This will probably have to be encapsulated eventually, like bitmap's creations
    // (also because import is a MEEEESSSSSSSSS)
    // (Actually, with bitmap_overlay now, we may just be able to call create whenever)
    // (Although file-backed stuff will throw a wrench in it, but it's VERY different (just an fd?))
    // (file-backing is a headache for some other day)

    block_store_t *bs = calloc(sizeof(block_store_t), 1);
    if (bs) {
        if ((bs->data_blocks = calloc(BLOCK_COUNT, BLOCK_SIZE)) &&
                // Eh, calloc, why not (technically a security risk if we don't)
                (bs->fbm = bitmap_overlay(BLOCK_COUNT, bs->data_blocks)) &&
                (bs->dbm = bitmap_create(BLOCK_COUNT))) {
            for (size_t idx = 0; idx < FBM_BLOCK_COUNT; ++idx) {
                bitmap_set(bs->fbm, idx);
            }
            bitmap_format(bs->dbm, 0xFF);
            // we have never synced, mark all as changed
            bs->flags = DIRTY;
            bs->fd = -1;
            bs_errno = BS_OK;
            return bs;
        }
        free(bs->data_blocks);
        bitmap_destroy(bs->dbm);
        bitmap_destroy(bs->fbm);
        free(bs);
    }
    bs_errno = BS_MEMORY;
    return NULL;
}

void block_store_destroy(block_store_t *const bs, const bs_flush_flag flush) {
    if (bs) {
        // If we aren't linked, no problem
        // If we ARE, flush if they asked AND unlink
        block_store_unlink(bs, flush);

        bitmap_destroy(bs->fbm);
        bitmap_destroy(bs->dbm);

        free(bs->data_blocks);
        free(bs);
        if (!flush) {
            // flush result takes priority of our standard OK
            // I'd just put this up at the unlink but
            // I really prefer setting the errno before the return
            // to make sure it's always "correct"
            bs_errno = BS_OK; // Haha, is it really ok if we just deleted it?
        }
        return;
    }
    bs_errno = BS_PARAM;
}

size_t block_store_allocate(block_store_t *const bs) {
    if (bs) {
        size_t free_block = bitmap_ffz(bs->fbm);
        if (free_block != SIZE_MAX) {
            bitmap_set(bs->fbm, free_block);
            bitmap_set(bs->dbm, FBM_BLOCK_CHANGE_LOCATION(free_block));
            // Set that FBM block as changed
            FLAG_SET(bs, DIRTY);
            bs_errno = BS_OK;
            return free_block;
        }
        bs_errno = BS_FULL;
        return 0;
    }
    bs_errno = BS_PARAM;
    return 0;
}


bool block_store_request(block_store_t *const bs, const size_t block_id) {
    if (bs && BLOCKID_VALID(block_id)) {
        if (!bitmap_test(bs->fbm, block_id)) {
            bitmap_set(bs->fbm, block_id);
            bitmap_set(bs->dbm, FBM_BLOCK_CHANGE_LOCATION(block_id));
            // Set that FBM block as changed
            FLAG_SET(bs, DIRTY);
            bs_errno = BS_OK;
            return true;
        } else {
            bs_errno = BS_IN_USE;
            return false;
        }
    }
    bs_errno = BS_PARAM;
    return false;
}


size_t block_store_get_used_blocks(const block_store_t *const bs) {
    if (bs) {
        return bitmap_total_set(bs->fbm) - FBM_BLOCK_COUNT; // stupid total_set undoes our FBM negation in total
    }
    return 0;
}

size_t block_store_get_free_blocks(const block_store_t *const bs) {
    if (bs) {
        return BLOCK_COUNT - bitmap_total_set(bs->fbm);
    }
    return 0;
}

size_t block_store_get_total_blocks() {
    return BLOCK_COUNT - FBM_BLOCK_COUNT;
}


void block_store_release(block_store_t *const bs, const size_t block_id) {
    if (bs && BLOCKID_VALID(block_id)) {
        // we could clear the dirty bit, since the info is no longer in use but...
        // We'll keep it. Could be useful. Doesn't really hurt anything.
        // Keeps it more true to a standard block device.
        // You could also use this function to format the specified block for security reasons
        bitmap_reset(bs->fbm, block_id);
        bitmap_set(bs->dbm, FBM_BLOCK_CHANGE_LOCATION(block_id));
        FLAG_SET(bs, DIRTY);
        bs_errno = BS_OK;
        return;
    }
    bs_errno = BS_PARAM;
}


size_t block_store_read(const block_store_t *const bs, const size_t block_id, void *buffer, const size_t nbytes, const size_t offset) {
    if (bs && BLOCKID_VALID(block_id) && buffer && nbytes && (nbytes + offset <= BLOCK_SIZE)) {
        // Not going to forbid reading of not-in-use blocks (but we'll log it via the errno)
        memcpy(buffer, bs->data_blocks + BLOCK_OFFSET_POSITION(block_id, offset), nbytes);
        bs_errno = bitmap_test(bs->fbm, block_id) ? BS_OK : BS_REQUEST_MISMATCH;
        return nbytes;
    }
    // technically we return BS_PARAM even if the internal structure of the BS object is busted
    // Which, in reality, would be more of a BS_INTERNAL or a BS_FATAL... but it'll add another branch to everything
    // And technically the bs is a parameter...
    bs_errno = BS_PARAM;
    return 0;
}

// Need to refactor this for V3. Have this switch on FILE_BASED and have it either do
// block_mem_write or block_file_write (both with same params) which then handles everything
// (same for read)
size_t block_store_write(block_store_t *const bs, const size_t block_id, const void *buffer, const size_t nbytes, const size_t offset) {
    if (bs && BLOCKID_VALID(block_id) && buffer && nbytes && (nbytes + offset <= BLOCK_SIZE)) {
        // Not going to forbid writing of not-in-use blocks (but we'll log it via errno)
        bitmap_set(bs->dbm, block_id);
        FLAG_SET(bs, DIRTY);
        memcpy((void *)(bs->data_blocks + BLOCK_OFFSET_POSITION(block_id, offset)), buffer, nbytes);
        bs_errno = bitmap_test(bs->fbm, block_id) ? BS_OK : BS_REQUEST_MISMATCH;
        return nbytes;
    }
    bs_errno = BS_PARAM;
    return 0;
}


block_store_t *block_store_import(const char *const filename) {
    block_store_t *bs = NULL;
    int fd = 0;
    if (filename) {
        struct stat file_stat;
        // macro count * size ?
        if (!stat(filename, &file_stat) && file_stat.st_size == (BLOCK_COUNT * BLOCK_SIZE)) {
            fd = open(filename, O_RDONLY);
            if (fd != -1) {
                bs = block_store_create();
                if (bs) {
                    if (utility_read_file(fd, bs->data_blocks, BLOCK_COUNT * BLOCK_SIZE) == BLOCK_COUNT * BLOCK_SIZE) {
                        // We're good to go, attempt to link.

                        close(fd);
                        // manual override because I'm not going to call link
                        //  and just have it write what we just read back out
                        bs->fd = open(filename, O_WRONLY);
                        if (bs->fd != -1) {
                            // Wipe it, we have a link to something we JUST read
                            // So it SHOULD be in sync with us unless something else is using the file
                            FLAG_SET(bs, FILE_LINKED);
                            FLAG_CLEAR(bs, DIRTY);
                            bitmap_format(bs->dbm, 0x00);
                        }
                        bs_errno = ((bs->fd == -1) ? BS_NO_LINK : BS_OK);
                        return bs;
                    }
                    // WAY less branching and resource management
                    //  no real need for the dreaded goto for maintainability
                    bs_errno = BS_FILE_IO;
                    block_store_destroy(bs, BS_NO_FLUSH);
                    close(fd);
                    return NULL;
                }
                close(fd);
            }
        }
        bs_errno = BS_FILE_ACCESS;
        return NULL;
    }
    bs_errno = BS_PARAM;
    return NULL;
}

/*
    size_t block_store_export(const block_store_t *const bs, const char *const filename) {
        // Thankfully, this is less of a mess than import...
        // we're going to ignore dbm, we'll treat export like it's making a new copy of the drive
        if (filename && bs && bs->fbm && bs->data_blocks) {
            const int fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
            if (fd != -1) {
                if (utility_write_file(fd, bitmap_export(bs->fbm), FBM_BLOCK_COUNT * BLOCK_SIZE) == (FBM_BLOCK_COUNT * BLOCK_SIZE)) {
                    if (utility_write_file(fd, bs->data_blocks, BLOCK_SIZE * (BLOCK_COUNT - FBM_BLOCK_COUNT)) == (BLOCK_SIZE * (BLOCK_COUNT - FBM_BLOCK_COUNT))) {
                        block_store_errno = BS_OK;
                        close(fd);
                        return BLOCK_SIZE * BLOCK_COUNT;
                    }
                }
                block_store_errno = BS_FILE_IO;
                close(fd);
                return 0;
            }
            block_store_errno = BS_FILE_ACCESS;
            return 0;
        }
        block_store_errno = BS_PARAM;
        return 0;
    }
*/


void block_store_link(block_store_t *const bs, const char *const filename) {
    if (bs && filename) {
        if (! FLAG_CHECK(bs, FILE_LINKED)) {
            // Ok, I can make a giant complicated hunk of logic to:
            //   Create if it doesn't exist
            //   Increase size if smaller
            //   Decrease size if larger
            // and It'll be a giant headache for various reasons (error checking, portability)
            // OR, I can just do it in two commands and call it a day.
            bs->fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
            if (bs->fd != -1) {
                if (utility_write_file(bs->fd, bs->data_blocks, BLOCK_COUNT * BLOCK_SIZE) == BLOCK_COUNT * BLOCK_SIZE) {
                    // Kill the DBM and dirty flag, set link state
                    bitmap_format(bs->dbm, 0x00);
                    FLAG_CLEAR(bs, DIRTY);
                    FLAG_SET(bs, FILE_LINKED);
                    bs_errno = BS_OK;
                    return;
                }
                bs_errno = BS_FILE_IO;
                return;
            }
            bs_errno = BS_FILE_ACCESS;
            return;
        }
        bs_errno = BS_LINK_EXISTS;
        return;
    }
    bs_errno = BS_PARAM;
}

void block_store_unlink(block_store_t *const bs, const bs_flush_flag flush) {
    if (bs) {
        if (FLAG_CHECK(bs, FILE_LINKED)) {
            if (flush) {
                // WE TRIED
                block_store_flush(bs);
            }
            // Eh, if close breaks, we can't help it.
            close(bs->fd);
            FLAG_CLEAR(bs, FILE_LINKED);
            bs->fd = -1;
            if (!flush) {
                bs_errno = BS_OK;
            }
            return;
        }
        bs_errno = BS_NO_LINK;
        return;
    }
    bs_errno = BS_PARAM;
}

void block_store_flush(block_store_t *const bs) {
    if (bs) {
        if (FLAG_CHECK(bs, FILE_LINKED)) {
            if (FLAG_CHECK(bs, DIRTY)) { // actual work to do
                // size_t blocks_to_write = bitmap_total_set(bs->dbm);
                /*
                    typedef struct {
                        int disaster_errno;
                        block_store_t *const bs;
                        size_t byte_counter;
                        bs_status status;
                    } bs_sync_obj;
                */
                bs_sync_obj sync_results = {0, bs, 0, BS_OK};

                bitmap_for_each(bs->dbm, &block_sync, &sync_results);
                if (sync_results.status == BS_OK) {
                    // Well it worked, hopefully
                    // Sipe the DBM and clear the dirty bit
                    bitmap_format(bs->dbm, 0x00);
                    FLAG_CLEAR(bs, DIRTY);
                }
                bs_errno = sync_results.status;
                return;
            }
            bs_errno = BS_OK;
            return;
        }
        bs_errno = BS_NO_LINK;
        return;
    }
    bs_errno = BS_PARAM;
    return;
}


const char *block_store_strerror(bs_status bs_err) {
    switch (bs_err) {
        case BS_OK:
            return "Ok";
        case BS_PARAM:
            return "Parameter error";
        case BS_INTERNAL:
            return "Internal error category (should not be returned)";
        case BS_FULL:
            return "Device full";
        case BS_IN_USE:
            return "Block in use";
        case BS_NOT_IN_USE:
            return "Block not in use";
        case BS_NO_LINK:
            return "Device not linked or could not link to file";
        case BS_LINK_EXISTS:
            return "Device already linked";
        case BS_FATAL:
            return "Fatal error category (should not be returned)";
        case BS_FILE_ACCESS:
            return "Could not access file or file not expected size";
        case BS_FILE_IO:
            return "Error during disk I/O";
        case BS_MEMORY:
            return "Memory allocation failure";
        case BS_WARN:
            return "Warning category (should not be returned)";
        case BS_REQUEST_MISMATCH:
            return "Read/write request to a block not marked in use";
        default:
            return "???";
    }
}


bs_status block_store_errno() {
    return bs_errno;
}


//
///
// HERE BE DRAGONS
///
//


// Block sync function to feed to bitmap_for_each
// Jumps the fd to the needed location and writes to it
// Admittedly, this function is not pretty.
void block_sync(size_t block_id, void *bs_sync_ptr) {
    bs_sync_obj *bs_sync = (bs_sync_obj *)bs_sync_ptr;
    /*
        typedef struct {
            int disaster_errno;
            block_store_t *const bs;
            size_t byte_counter;
            bs_status status;
        } bs_sync_obj;
    */
    if (bs_errno == BS_OK) {
        if (bs_sync && bs_sync->status == BS_OK) {
            if (bs_sync->bs) {
                if (FLAG_CHECK(bs_sync->bs, FILE_LINKED)) {
                    // Ducks = in a row
                    // jump to file position
                    if (lseek(bs_sync->bs->fd, BLOCK_POSITION(block_id), SEEK_SET) == BLOCK_POSITION(block_id)) {
                        // attempt to write
                        size_t written = utility_write_file(bs_sync->bs->fd, bs_sync->bs->data_blocks + BLOCK_POSITION(block_id), BLOCK_SIZE);
                        // Update written with WHATEVER happened
                        bs_sync->byte_counter += written;
                        if (written == BLOCK_SIZE) {
                            // all is ok, we wrote everything, or so we were told
                            return;
                        }
                    }
                    bs_sync->status = BS_FILE_IO;
                    // save whatever errno was generated by seek or write
                    bs_sync->disaster_errno = errno;
                    return;
                }
                bs_sync->status = BS_NO_LINK; // HOW DID THIS HAPPEN
                return;
            }
            bs_sync->status = BS_PARAM;
        }
        bs_errno = BS_PARAM;
    }
    return;
}

size_t utility_read_file(const int fd, uint8_t *buffer, const size_t count) {
    // reads in the requested ammount of data
    // Attempts to read all at once, will attempt to retry and finish read
    // A smarter version will automatically block the data into chunks (4k?)
    size_t have_read = 0, will_read = count;
    ssize_t data_read;
    do {
        data_read = read(fd, buffer + have_read, will_read);
        if (data_read == -1) {
            if (errno == EINTR) { continue; }
            return have_read;
        }
        will_read -= data_read;
        have_read += data_read;
    } while (will_read);
    return have_read;
}

size_t utility_write_file(const int fd, const uint8_t *buffer, const size_t count) {
    size_t have_written = 0, will_write = count;
    ssize_t data_written;
    do {
        data_written = write(fd, buffer + have_written, will_write);
        if (data_written == -1) {
            if (errno == EINTR) { continue; }
            return have_written;
        }
        will_write -= data_written;
        have_written += data_written;
    } while (will_write);
    return have_written;
}