#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../src/block_store.c"
// including the .c lets's us see the inner working and test things easier
// than if we were using the public interface
// (you can only see inside the struct if you do it this way)

#define assert(e) ((e) ? (true) : \
                   (fprintf(stderr,"%s,%d: assertion '%s' failed\n",__FILE__, __LINE__, #e), \
                    fflush(stdout), abort()))

/*

    block_store_t *block_store_create();
    1. NORMAL. Assert all contents and errno
    ... that's it, we can't do more without a better testing system

    void block_store_destroy(block_store_t *const bs, const bs_flush_flag flush);
    1. NORMAL, no flush
    2. NORMAL, flush
    SPECIAL 3. FAIL, null bs. Assert we don't crash and bs_errno is set

    size_t block_store_allocate(block_store_t *const bs);
    1. NORMAL, Assert can allocate all blocks, check errno and fbm, dbm
    2. FAIL, Fail on full, check errno
    3. FAIL, null bs, check errno

    size_t block_store_get_used_blocks(const block_store_t *const bs);
    1. NORMAL, empty
    2. NORMAL, some
    3. NORMAL, full
    2. FAIL, NULL

    size_t block_store_get_free_blocks(const block_store_t *const bs);
    1. NORMAL, empty
    2. NORMAL, some
    3. NORMAL, full
    2. FAIL, NULL

    size_t block_store_get_total_blocks();
    1. NORMAL ... cannot fail (for now)

    void block_store_release(block_store_t *const bs, const size_t block_id);
    1. NORMAl, assert FBM/DBM and errno
    2. FAIL, reserved block, assert fbm/sbm/errno
    3. FAIL, null bs, assert errno

    size_t block_store_read(const block_store_t *const bs, const size_t block_id, void *buffer, const size_t nbytes, const size_t offset);
    1. NORMAL, assert arbitrary read contents, check errno
    2. NORMAL, non-allocated block, check errno
    3. NORMAL, full block read
    4. FAIL, full+1 block read
    5. FAIL, zero nbytes
    6. FAIL, offset at end & zero nbytes
    7. FAIL, offset > size
    8. FAIL, offset + nbytes > size
    9. FAIL, null bs
    10. FAIL, null buffer

    size_t block_store_write(block_store_t *const bs, const size_t block_id, const void *buffer, const size_t nbytes, const size_t offset);
    See read

    block_store_t *block_store_import(const char *const filename);
    1. NORMAL
    2. FAIL, file does not exist
    3. FAIL, bad filename
    4. FAIL, null filename
    5. FAIL, bad file size

    EXPORT IS DEAD, LONG LIVE THE LINK
    size_t block_store_export(const block_store_t *const bs, const char *const filename);
    1. NORMAL
    2. FAIL, bad filename (...? is that a thing?)
    3. FAIL, NULL filename
    4. FAIL, null bs

    void block_store_link(block_store_t *const bs, const char *const filename);
    1. NORMAL, ...? Some file - check DBM is cleared
    2. FAIL, already linked - check DBM not clear
    3. FAIL, NULL filename - check DBM not clear
    4. FAIL, NULL object

    void block_store_unlink(block_store_t *const bs, const bs_flush_flag flush);
    1. NORMAL, no flush
    2. NORMAL, flush
    2. FAIL, no link
    3. FAIL, NULL

    void block_store_flush(block_store_t *const bs);
    1. NORMAL, data to write
    2. NORMAL, no data to write
    3. FAIL, no link
    4. FAIL, NULL object

*/

// ALLOCATE RELEASE CREATE DESTROY(NON-SPECIAL) GET_USED GET_FREE GET_TOTAL
void basic_tests_a();

// IMPORT EXPORT
void basic_tests_b();

// READ WRITE
void basic_tests_c();

//  LINK UNLINK FLUSH DESTROY(SPECIAL)
void basic_tests_d();

int main() {

    puts("Running autotests, sit back and relax, it'll be awhile...");

    basic_tests_a();

    puts("A tests passed...");

    basic_tests_b();

    puts("B tests passed...");

    basic_tests_c();

    puts("C tests passed...");

    // These tests are kinda spotty
    basic_tests_d();

    puts("D tests passed...");

    puts("TESTS COMPLETE");

}

void basic_tests_a() {

    block_store_t *bs_a = NULL;

    // CREATE 1
    // ALLOCATE 1 2
    // RELEASE 1 2

    // Check valid creation (can't test a failed one without breaking selectively breaking malloc)
    assert(bs_a = block_store_create());
    assert(bs_errno == BS_OK);

    // assert dbm, fbm, and fd

    assert(bs_a->fd == -1);

    for (size_t i = 0; i < FBM_BLOCK_COUNT; ++i) {
        assert(bitmap_test(bs_a->fbm, i));
        assert(bitmap_test(bs_a->dbm, i));
    }

    // GET_USED GET_FREE GET_TOTAL 1
    assert(block_store_get_total_blocks() == BLOCK_COUNT - FBM_BLOCK_COUNT);
    assert(block_store_get_used_blocks(bs_a) == 0);
    assert(block_store_get_free_blocks(bs_a) == BLOCK_COUNT - FBM_BLOCK_COUNT);

    // GET_TOTAL tested and cleared for use

    // ALLOCATE 1

    // Check allocate up to full & fail
    for (size_t i = FBM_BLOCK_COUNT; i < BLOCK_COUNT; ++i) {
        assert(bitmap_test(bs_a->fbm, i) == false);
        assert(bitmap_test(bs_a->dbm, i));

        assert(i == block_store_allocate(bs_a));
        assert(bs_errno == BS_OK);

        assert(bitmap_test(bs_a->fbm, i));
        assert(bitmap_test(bs_a->dbm, i));
        assert(bitmap_test(bs_a->dbm, FBM_BLOCK_CHANGE_LOCATION(i)));
    }

    // GET_USED GET_FREE 3
    assert(block_store_get_free_blocks(bs_a) == 0);
    assert(block_store_get_used_blocks(bs_a) == block_store_get_total_blocks());

    // ALLOCATE 2

    assert(block_store_allocate(bs_a) == 0);
    assert(bs_errno == BS_FULL);
    assert(bs_errno == block_store_errno()); // Tiny test to validate that the two are in sync

    // Arbitrary release and reallocate

    block_store_release(bs_a, (BLOCK_COUNT >> 3) + 5);
    assert(bs_errno == BS_OK);

    // GET_USED GET_FREE 2
    assert(block_store_get_free_blocks(bs_a) == 1);
    assert(block_store_get_used_blocks(bs_a) == block_store_get_total_blocks() - 1);

    // GET_USED AND GET_FREE tested and cleared

    assert(bitmap_test(bs_a->fbm, (BLOCK_COUNT >> 3) + 5) == false);
    assert(bitmap_test(bs_a->dbm, (BLOCK_COUNT >> 3) + 5));
    assert(bitmap_test(bs_a->dbm, FBM_BLOCK_CHANGE_LOCATION((BLOCK_COUNT >> 3) + 5)));

    assert(block_store_allocate(bs_a) == ((BLOCK_COUNT >> 3) + 5));
    assert(bs_errno == BS_OK);

    // Free back to empty

    for (size_t i = BLOCK_COUNT; i > FBM_BLOCK_COUNT; --i) {
        assert(bitmap_test(bs_a->fbm, i - 1));
        assert(bitmap_test(bs_a->dbm, i - 1));

        block_store_release(bs_a, i - 1);
        assert(bs_errno == BS_OK);

        assert(bitmap_test(bs_a->fbm, i - 1) == false);
        assert(bitmap_test(bs_a->dbm, i - 1));
    }

    for (size_t i = 0; i < FBM_BLOCK_COUNT; ++i) {
        assert(bitmap_test(bs_a->fbm, i));
        assert(bitmap_test(bs_a->dbm, i));
    }

    // CREATE tested and clear for use

    // RELEASE 2
    block_store_release(bs_a, 0);
    assert(bs_errno == BS_PARAM);

    assert(bitmap_test(bs_a->fbm, 0));
    assert(bitmap_test(bs_a->dbm, 0));

    block_store_release(bs_a, FBM_BLOCK_COUNT - 1);
    assert(bs_errno == BS_PARAM);

    assert(bitmap_test(bs_a->fbm, FBM_BLOCK_COUNT - 1));
    assert(bitmap_test(bs_a->dbm, FBM_BLOCK_COUNT - 1));

    // RELEASE 3

    block_store_release(NULL, 0);
    assert(bs_errno == BS_PARAM);

    // ALLOCATE 3

    assert(0 == block_store_allocate(NULL));
    assert(bs_errno == BS_PARAM);

    // RELEASE tested and clear for use
    // ALLOCATE tested and clear for use

    // DESTROY 2

    block_store_destroy(NULL, BS_NO_FLUSH);
    assert(bs_errno == BS_PARAM);

    // DESTROY 1

    block_store_destroy(bs_a, BS_NO_FLUSH);
    assert(bs_errno == BS_OK);

    // DESTROY(NON-SPECIAL) tested and clear for use
}


// IMPORT EXPORT
void basic_tests_b() {
    block_store_t *bs_a = NULL, *bs_b = NULL;
    const char *const file = "test.bs";
    //uint16_t arr[BLOCK_SIZE >> 1];


    assert(0 == system("./generate_drive e test.bs"));
    assert(0 == system("echo 4 8 15 16 23 42 > bad.bs"));
    puts("IGNORE ME!!!");
    assert(system("ls DOESNOTEXIST.bs"));


    // IMPORT 1

    bs_a = block_store_import(file);
    assert(bs_a);
    assert(bs_errno == BS_OK);

    for (size_t i = 0; i < FBM_BLOCK_COUNT; ++i) {
        assert(bitmap_test(bs_a->fbm, i));
        assert(bitmap_test(bs_a->dbm, i) == false);
    }

    for (size_t i = BLOCK_COUNT; i > FBM_BLOCK_COUNT; --i) {
        assert(bitmap_test(bs_a->fbm, i - 1) == false);
        assert(bitmap_test(bs_a->dbm, i - 1) == false);
    }

    // IMPORT 2

    bs_b = block_store_import("DOESNOTEXIST.bs");
    assert(bs_b == NULL);
    assert(bs_errno == BS_FILE_ACCESS);

    // IMPORT 4

    bs_b = block_store_import(NULL);
    assert(bs_b == NULL);
    assert(bs_errno == BS_PARAM);

    // Actually not sure what will happen here...
    // IMPORT 3

    bs_b = block_store_import("\n");
    assert(bs_b == NULL);
    assert(bs_errno == BS_FILE_ACCESS);

    //IMPORT 5
    bs_b = block_store_import("bad.bs");
    assert(bs_b == NULL);
    assert(bs_errno == BS_FILE_ACCESS);

    // IMPORT tested and cleared for use

    /*
        // EXPORT 1
        size_t ret_size = block_store_export(bs_a, "new_test.bs");
        assert(ret_size == (BLOCK_COUNT * BLOCK_SIZE));
        assert(bs_errno == BS_OK);
        assert(0 == system("diff test.bs new_test.bs"));

        // EXPORT 2

        // not sure what this will do...
        // It makes a file with a messed up name. Hmm.
        //assert(block_store_export(bs_a, "\n") == 0);
        //assert(bs_errno == BS_FILE_ACCESS);
        // WONTFIX, name sanitation isn't our problem. If the OS allows it, so should we

        assert(block_store_export(bs_a, "") == 0);
        assert(bs_errno == BS_FILE_ACCESS);

        // EXPORT 3

        assert(block_store_export(bs_a, NULL) == 0);
        assert(bs_errno == BS_PARAM);

        // EXPORT 4
        assert(block_store_export(NULL, "should_not_create.bs") == 0);
        assert(bs_errno == BS_PARAM);

    */



    block_store_destroy(bs_a, BS_NO_FLUSH);

    system("rm test.bs bad.bs should_not_create.bs new_test.bs");

}


// READ WRITE
void basic_tests_c() {

    block_store_t *bs_a = NULL;
    uint16_t arr[(BLOCK_SIZE >> 1) + 128];
    const char *const file = "test.bs";
    assert(0 == system("./generate_drive e test.bs"));

    bs_a = block_store_import(file);
    assert(bs_a);


    size_t blk_id = block_store_allocate(bs_a);
    assert(blk_id);


    // READ 3

    size_t res_size = block_store_read(bs_a, blk_id, arr, BLOCK_SIZE, 0);
    assert(bs_errno == BS_OK);
    assert(res_size == BLOCK_SIZE);
    assert(memcmp(arr, bs_a->data_blocks + (BLOCK_SIZE * blk_id), BLOCK_SIZE) == 0);

    // READ 1

    size_t arb_size = (BLOCK_SIZE >> 2) + 7;
    size_t arb_offset = BLOCK_SIZE >> 3;
    res_size = block_store_read(bs_a, blk_id, arr, arb_size, arb_offset);
    assert(memcmp(arr, bs_a->data_blocks + (BLOCK_SIZE * blk_id), arb_size) == 0);
    assert(bs_errno == BS_OK);
    assert(res_size == (BLOCK_SIZE >> 2) + 7);

    // READ 2

    res_size = block_store_read(bs_a, blk_id + 1, arr, BLOCK_SIZE, 0);
    assert(bs_errno == BS_REQUEST_MISMATCH);
    assert(res_size ==  BLOCK_SIZE);
    assert(memcmp(arr, bs_a->data_blocks + (BLOCK_SIZE * (blk_id + 1)), BLOCK_SIZE) == 0);

    // READ 4

    res_size = block_store_read(bs_a, blk_id, arr, BLOCK_SIZE + 1, 0);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // READ 5

    res_size = block_store_read(bs_a, blk_id, arr, 0, 0);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // READ 6

    res_size = block_store_read(bs_a, blk_id, arr, 0, BLOCK_SIZE);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // READ 7

    res_size = block_store_read(bs_a, blk_id, arr, 1, BLOCK_SIZE + 1);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // READ 8

    res_size = block_store_read(bs_a, blk_id, arr, (BLOCK_SIZE >> 1) + 1, (BLOCK_SIZE >> 1) + 1);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // READ 9

    res_size = block_store_read(NULL, blk_id, arr, BLOCK_SIZE >> 2, 0);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // READ 10

    res_size = block_store_read(bs_a, blk_id, NULL, BLOCK_SIZE >> 2, 0);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // READ tests complete and clear for use

    // WRITE 3

    res_size = block_store_write(bs_a, blk_id, arr, BLOCK_SIZE, 0);
    assert(bs_errno == BS_OK);
    assert(res_size == BLOCK_SIZE);
    assert(FLAG_CHECK(bs_a, DIRTY));
    assert(memcmp(arr, bs_a->data_blocks + (BLOCK_SIZE * blk_id), BLOCK_SIZE) == 0);
    assert(bitmap_test(bs_a->dbm, blk_id));

    // WRITE 1

    arb_size = (BLOCK_SIZE >> 2) + 7;
    arb_offset = BLOCK_SIZE >> 3;
    res_size = block_store_write(bs_a, blk_id, arr, arb_size, arb_offset);
    assert(memcmp(arr, bs_a->data_blocks + (BLOCK_SIZE * blk_id), arb_size) == 0);
    assert(bs_errno == BS_OK);
    assert(FLAG_CHECK(bs_a, DIRTY));
    assert(res_size == (BLOCK_SIZE >> 2) + 7);
    assert(bitmap_test(bs_a->dbm, blk_id));

    // WRITE 2

    res_size = block_store_write(bs_a, blk_id + 1, arr, BLOCK_SIZE, 0);
    assert(bs_errno == BS_REQUEST_MISMATCH);
    assert(res_size ==  BLOCK_SIZE);
    assert(FLAG_CHECK(bs_a, DIRTY));
    assert(bitmap_test(bs_a->dbm, blk_id + 1));
    assert(memcmp(arr, bs_a->data_blocks + (BLOCK_SIZE * (blk_id + 1)), BLOCK_SIZE) == 0);

    // WRITE 4

    res_size = block_store_write(bs_a, blk_id, arr, BLOCK_SIZE + 1, 0);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // WRITE 5

    res_size = block_store_write(bs_a, blk_id, arr, 0, 0);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // WRITE 6

    res_size = block_store_write(bs_a, blk_id, arr, 0, BLOCK_SIZE);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // WRITE 7

    res_size = block_store_write(bs_a, blk_id, arr, 1, BLOCK_SIZE + 1);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // WRITE 8

    res_size = block_store_write(bs_a, blk_id, arr, (BLOCK_SIZE >> 1) + 1, (BLOCK_SIZE >> 1) + 1);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // WRITE 9

    res_size = block_store_write(NULL, blk_id, arr, BLOCK_SIZE >> 2, 0);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    // WRITE 10

    res_size = block_store_write(bs_a, blk_id, NULL, BLOCK_SIZE >> 2, 0);
    assert(bs_errno == BS_PARAM);
    assert(res_size ==  0);

    block_store_destroy(bs_a, BS_NO_FLUSH);

    system("rm test.bs");


}

void basic_tests_d() {

    block_store_t *bs_a;
    const char *const file = "test.bs";
    assert(0 == system("./generate_drive e test.bs"));

    bs_a = block_store_import(file);
    assert(bs_a);
    assert(!FLAG_CHECK(bs_a, DIRTY));

    // UNLINK 1
    block_store_unlink(bs_a, BS_NO_FLUSH);
    assert(bs_errno == BS_OK);


    // LINk 1
    block_store_link(bs_a, file);
    assert(bs_errno == BS_OK);
    assert(FLAG_CHECK(bs_a, FILE_LINKED));
    assert(!FLAG_CHECK(bs_a, DIRTY));
    assert(bs_a->fd != -1);
    for (size_t i = 0; i < BLOCK_COUNT; ++i) {
        assert(!bitmap_test(bs_a->dbm, i));
    }

    // LINK 2
    block_store_link(bs_a, file);
    assert(bs_errno == BS_LINK_EXISTS);
    assert(FLAG_CHECK(bs_a, FILE_LINKED));
    assert(bs_a->fd != -1);

    // LINK 3
    block_store_link(bs_a, NULL);
    assert(bs_errno == BS_PARAM);
    assert(FLAG_CHECK(bs_a, FILE_LINKED));

    // LINK 4
    block_store_link(NULL, file);
    assert(bs_errno == BS_PARAM);

    // UNLINK 2
    block_store_unlink(bs_a, BS_FLUSH);
    assert(bs_errno == BS_OK);

    // UNLINK 3
    block_store_unlink(bs_a, BS_FLUSH);
    assert(bs_errno == BS_NO_LINK);

    // UNLINK 4
    block_store_link(NULL, file);
    assert(bs_errno == BS_PARAM);


    // UNLINK and LINK tested and cleared for use

    /*
        size_t block_store_flush(block_store_t *const bs);
        1. NORMAL, data to write
        2. NORMAL, no data to write
        3. FAIL, no link
        4. FAIL, NULL object
    */

    block_store_link(bs_a, "new_test.bs");
    assert(bs_errno == BS_OK);
    assert(!FLAG_CHECK(bs_a, DIRTY));

    block_store_flush(bs_a);
    assert(bs_errno == BS_OK);
    assert(0 == system("diff test.bs new_test.bs"));

    // A minor DBM check since the create function always formats the DBM
    size_t allocated = block_store_allocate(bs_a);
    assert(allocated);
    assert(bs_errno == BS_OK);
    assert(FLAG_CHECK(bs_a, DIRTY));
    assert(bitmap_test(bs_a->dbm, FBM_BLOCK_CHANGE_LOCATION(allocated)));

    // Just write some data to the allocated block
    assert(block_store_write(bs_a, allocated, bitmap_export(bs_a->fbm), BLOCK_SIZE, 0) == BLOCK_SIZE);

    // FLUSH 1
    block_store_flush(bs_a);
    assert(bs_errno == BS_OK);
    // they now differ, but we'd have to check actual data manually
    assert(system("diff test.bs new_test.bs"));

    // FLUSH 2
    block_store_flush(bs_a);
    assert(bs_errno == BS_OK);

    // FLUSH 3
    block_store_unlink(bs_a, BS_NO_FLUSH);
    assert(bs_errno == BS_OK);
    block_store_flush(bs_a);
    assert(bs_errno == BS_NO_LINK);

    // FLUSH 4
    block_store_flush(NULL);
    assert(bs_errno == BS_PARAM);

    // FLush tested and clear for use (?)

    system("rm test.bs new_test.bs");

}
