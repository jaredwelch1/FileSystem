#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
#include <dyn_array.h>
using std::vector;
using std::string;

#include <gtest/gtest.h>

#include "f16fs.h"

unsigned int score;
unsigned int total;

class GradeEnvironment : public testing::Environment {
  public:
    virtual void SetUp() {

        score = 0;
        
#if GRAD_TESTS
        total = 20;
#else
        total = 20;
#endif
    }

    virtual void TearDown() {
        ::testing::Test::RecordProperty("points_given", score);
        ::testing::Test::RecordProperty("points_total", total);
        std::cout << "SCORE: " << score << '/' << total << std::endl;
    }
};

bool find_in_directory(const dyn_array_t *const record_arr, const char *fname) {
    if (record_arr && fname) {
        for (size_t i = 0; i < dyn_array_size(record_arr); ++i) {
            if (strncmp(((file_record_t *) dyn_array_at(record_arr, i))->name, fname, FS_FNAME_MAX) == 0) {
                return true;
            }
        }
    }
    return false;
}

/*

F16FS_t * fs_format(const char *const fname);
    1   Normal
    2   NULL
    3   Empty string

F16FS_t *fs_mount(const char *const fname);
    1   Normal
    2   NULL
    3   Empty string

int fs_unmount(F16FS_t *fs);
    1   Normal
    2   NULL

*/

TEST(a_tests, format_mount_unmount) {
    const char *test_fname = "a_tests.f16fs";

    F16FS_t *fs = NULL;

    // FORMAT 2
    ASSERT_EQ(fs_format(NULL), nullptr);

    // FORMAT 3
    // this really should just be caught by block_store
    ASSERT_EQ(fs_format(""), nullptr);

    // FORMAT 1
    fs = fs_format(test_fname);
    ASSERT_NE(fs, nullptr);

    // UNMOUNT 1
    ASSERT_EQ(fs_unmount(fs), 0);

    // UNMOUNT 2
    ASSERT_LT(fs_unmount(NULL), 0);

    // MOUNT 1
    fs = fs_mount(test_fname);
    ASSERT_NE(fs, nullptr);
    fs_unmount(fs);

    // MOUNT 2
    ASSERT_EQ(fs_mount(NULL), nullptr);

    // MOUNT 3
    ASSERT_EQ(fs_mount(""), nullptr);

    score += 20;
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new GradeEnvironment);
    return RUN_ALL_TESTS();
}
