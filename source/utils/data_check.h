#ifndef GUNBROS_DATA_CHECK_H
#define GUNBROS_DATA_CHECK_H

#include <stdbool.h>

void gunbros_print_data_check(void);
unsigned int gunbros_get_pack_toc_expectation_count(void);
bool gunbros_get_pack_toc_expectation(unsigned short pack_index,
                                      unsigned int *entry_count,
                                      unsigned int *digest,
                                      const char **pack_name);
bool gunbros_get_pack_game_object_tables(unsigned short pack_index,
                                         const unsigned char **type_counts,
                                         unsigned int *type_count,
                                         const unsigned int **resource_indices,
                                         unsigned int *resource_index_count);

#endif
