#ifndef BLOCK_STORAGE_H__
#define BLOCK_STORAGE_H__

#include <stdint.h>
#include <bitmap.h> // This header comes from OSF15_Library, make sre you install it!
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

// Notice we don't use stdio and produce no output
// Libraries generally should leave stdio alone.
// Writing to stderr will be acceptable for big errors and debugging purposes
// but you WILL lose points if you write to stdout


// declaring the struct but not implementing it allows us to prevent users
//  from using the object directly and monkeying with the contents
// They can only create pointers to the struct, which must be given out by us
// This enforces a black box device
typedef struct block_store block_store_t;

/*
	Ok, here's a rundown of the idea I went with for this error system:
		We have an error enum with defferent categories of errors: OK, PARAM, INTERNAL, FATAL, and WARN.
		An error AND'd with it's error category will result in true and fail otherwise, allowing you to
			identify groups of errors and act accordingly
		You shouldn't actually set the errno to a category unless it's BS_OK, you should use the
			subcategory errors
		BS_OK is pretty self-explanitory
		BS_PARAM indicated a parameter error. I also, somewhat arguably incorrectly, also use it to
			indicate an error with the BS object, though that may technically be INTERNAL/FATAL
		BS_INTERNAL indicates some sort of issue with the BS object itself, but it's not a fatal issue
			For example, you wanted a block, but we're out. The object's not broken, but it is an error.
		BS_FATAL Originally was supposed to indicate that the BS object was left in a broken state, but
			it turned out that due to the way I organized things, the object can only break during
			creation, and if that happens, it's not returned, so now it indicated that the operation failed
			for some reason (like a malloc failure). Since I have both FILE_ACCESS and FILE_IO, let me explain
			FILE_ACCESS is for when you, well, can't access the file. FILE_IO is for an error during file IO
			(which is a much bigger problem, if this is during export, the file might be broken, and if it's
			dirng import, import fails)
		BS_WARN is really just covering an edge case I decided to allow
			BS_REQUEST_MISMATCH is set when you read/write to a block not marked as in use
			The read/write functions still return normally, but the errno isn't BS_OK.
			Warning are meant to be just that, only a warning
	Be sure to return the appropriate error category when a problem happens!
*/

///
/// Enum containing the different error codes
///
typedef enum {
    BS_OK = 0x00,
    BS_PARAM = 0x10,
    BS_INTERNAL = 0x20, BS_FULL = 0x21, BS_IN_USE = 0x22, BS_NOT_IN_USE = 0x23, BS_NO_LINK = 0x24, BS_LINK_EXISTS = 0x25,
    BS_FATAL = 0x40, BS_FILE_ACCESS = 0x41, BS_FILE_IO = 0x42, BS_MEMORY = 0x43,
    BS_WARN = 0x80, BS_REQUEST_MISMATCH = 0x81
} bs_status;

typedef enum {
    BS_NO_FLUSH = 0x00,
    BS_FLUSH = 0x01
} bs_flush_flag;

///
/// This creates a new BS device
/// \return Pointer to a new block storage device, NULL on error
///
block_store_t *block_store_create();

///
/// Destroys the provided block storage device
///  NOTE: If a flush is requested, it will only be ATTEMPTED.
///   If the flush fails for ANY reason, the object will still be destructed
///   If a flush is requested, the bs_errno will be set to the status of the flush operation
/// \param bs BS device
/// \param flush flag indicating whether a flush should be attempted before destruction
///
void block_store_destroy(block_store_t *const bs, const bs_flush_flag flush);

///
/// Searches for a free block, makes it as in use, and returns the block's id
/// \param bs BS device
/// \return Allocated block's id, 0 on error
///
size_t block_store_allocate(block_store_t *const bs);

///
/// Attempts to allocate the requested block id
/// \param bs the block store object
/// \block_id the requested block identifier
/// \return boolean indicating succes of operation
///
bool block_store_request(block_store_t *const bs, const size_t block_id);

///
/// Counts the number of blocks marked as in use by the user
/// \param bs BS device
/// \return Total blocks in use, 0 on error
///
size_t block_store_get_used_blocks(const block_store_t *const bs);

///
/// Counts the number of blocks marked free for use
/// (lie: calls used and inverts it because of bitmap limitation)
/// \param bs BS device
/// \return Total blocks free, 0 on error
///
size_t block_store_get_free_blocks(const block_store_t *const bs);

///
/// Returns the total number of user-addressable blocks
/// \return Total blocks
///
size_t block_store_get_total_blocks();

///
/// Frees the specified block
/// \param bs BS device
/// \param block_id The block to free
///
void block_store_release(block_store_t *const bs, const size_t block_id);

///
/// Reads data from the specified block and offset and writes it to the designated buffer
/// \param bs BS device
/// \param block_id Source block id
/// \param buffer Data buffer to write to
/// \param nbytes (non-zero) number of bytes to read
/// \param offset Block read offset
/// \return Number of bytes read, 0 on error
///
size_t block_store_read(const block_store_t *const bs, const size_t block_id, void *buffer, const size_t nbytes, const size_t offset);

///
/// Reads data from the specified buffer and writes it to the designated block and offset
/// \param bs BS device
/// \param block_id Destination block id
/// \param buffer Data buffer to read from
/// \param nbytes (non-zero) number of bytes to write
/// \param offset Block write offset
/// \return Number of bytes written, 0 on error
///
size_t block_store_write(block_store_t *const bs, const size_t block_id, const void *buffer, const size_t nbytes, const size_t offset);

///
/// Imports BS device from the given file and links to it
/// A linked file will be updated with the latest changes via flush()
/// (bs_errno is BS_NO_LINK if import was sccessful, but a link could not be established)
/// \param filename The file to load
/// \return Pointer to new BS device, NULL on error
///
block_store_t *block_store_import(const char *const filename);

///
/// Links and syncs the burrent state of the block store with the new file
///  If the file does not extist, it will be created
/// NOTE: Linking will fail if the file is already linked.
/// \param bs the block store object
/// \param filename the file to link to
///
void block_store_link(block_store_t *const bs, const char *const filename);

///
/// Unlinks and closes the file (if any) associated with the block store
///  NOTE: If a flush is requested, it will only be ATTEMPTED.
///   If the flush fails for ANY reason, the file will still be unlinked and
///   If a flush is requested, the bs_errno will be set to the status of the flush operation
/// \param bs the block store object
/// \param flush flag indicating where a flush should be attempted before unlink
///
void block_store_unlink(block_store_t *const bs, const bs_flush_flag flush);

///
/// Flushes changes to the block store to the linked file
/// \param bs the block store object
///
void block_store_flush(block_store_t *const bs);

///
/// Returns a string representing the error code given
/// \param bs_err Error status code
/// \return C-string with error message
///
const char *block_store_strerror(bs_status bs_err);

///
/// Returns the current errno, check out the enum and strerror
/// \return Current error
///
bs_status block_store_errno();


/*
    // PIT OF DEPRECATION:

	/// OH MAN THIS BROKE EVERYTHING AND I NEVER NOTICED AND I AM BAD A COMPUTERS
	///
	/// This is our errno... it's an errno.
	///
	static bs_status bs_errno = BS_OK;


    ///
    /// DEPRECATED
    /// Writes the entirety of the BS device to file, overwriting it if it exists
    /// \param bs BS device
    /// \param filename The file to write to
    /// \return Number of bytes written, 0 on error
    ///
    size_t block_store_export(const block_store_t *const bs, const char *const filename);
*/

#endif