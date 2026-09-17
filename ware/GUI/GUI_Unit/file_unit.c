#include "variables.h"
#include "defines.h"
#include "malloc.h"


void chosen_file_path_free(void)
{
    if (chosen_file_path) {
        free_bsc(chosen_file_path);
        chosen_file_path = NULL;
    }
}