#pragma once

#include <stdint.h>
	
	
#define MIN_QUEUE_SIZE 2
#define MQ_All_VALID_PAGE 0x0000000000000000ULL
#define DEFAULT_STREAM 0

enum class Queue_Type{
    HOT_QUEUE = 1,
    LAST_QUEUE = 2,
};
typedef uint64_t lui_timestamp;
typedef uint64_t LPA_type;
typedef uint32_t level_type;
