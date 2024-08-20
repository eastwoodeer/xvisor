#include "vmm_spinlocks.h"
#include <vmm_stdio.h>
#include <vmm_modules.h>
#include <vmm_heap.h>
#include <libs/stringlib.h>
#include <libs/coverage.h>

static vmm_spinlock_t coverage_lock;

/* INIT_SPIN_LOCK(&coverage_lock); */

static struct gcov_info *gcov_info_head;

void __gcov_init(struct gcov_info *info)
{
	info->next = gcov_info_head;
	gcov_info_head = info;
}

void __gcov_merge_add(gcov_type *counters, unsigned int n_counters)
{
	/* UNUSED */
}

void __gcov_exit(void)
{
	/* UNUSED */
}

/**
 * print_u8 - Print 8 bit of gcov data
 */
static inline void print_u8(u8 v)
{
	vmm_printf("%02x", v);
}

/**
 * print_u32 - Print 32 bit of gcov data
 */
static inline void print_u32(u32 v)
{
	u8 *ptr = (u8 *)&v;

	print_u8(*ptr);
	print_u8(*(ptr + 1));
	print_u8(*(ptr + 2));
	print_u8(*(ptr + 3));
}

/**
 * write_u64 - Store 64 bit data on a buffer and return the size
 */

static inline void write_u64(void *buffer, size_t *off, u64 v)
{
	if (buffer != NULL) {
		memcpy((u8 *)buffer + *off, (u8 *)&v, sizeof(v));
	} else {
		print_u32(*((u32 *)&v));
		print_u32(*(((u32 *)&v) + 1));
	}
	*off = *off + sizeof(u64);
}

/**
 * write_u32 - Store 32 bit data on a buffer and return the size
 */
static inline void write_u32(void *buffer, size_t *off, u32 v)
{
	if (buffer != NULL) {
		memcpy((u8 *)buffer + *off, (u8 *)&v, sizeof(v));
	} else {
		print_u32(v);
	}
	*off = *off + sizeof(u32);
}

size_t gcov_calculate_buff_size(struct gcov_info *info)
{
	u32 iter;
	u32 iter_1;
	u32 iter_2;

	/* Few fixed values at the start: magic number,
	 * version, stamp, and checksum.
	 */
#ifdef GCOV_12_FORMAT
	u32 size = sizeof(u32) * 4;
#else
	u32 size = sizeof(u32) * 3;
#endif

	for (iter = 0U; iter < info->n_functions; iter++) {
		/* space for TAG_FUNCTION and FUNCTION_LENGTH
		 * ident
		 * lineno_checksum
		 * cfg_checksum
		 */
		size += (sizeof(u32) * 5);

		for (iter_1 = 0U; iter_1 < GCOV_COUNTERS; iter_1++) {
			if (!info->merge[iter_1]) {
				continue;
			}

			/*  for function counter and number of values  */
			size += (sizeof(u32) * 2);

			for (iter_2 = 0U;
			     iter_2 < info->functions[iter]->ctrs->num;
			     iter_2++) {
				/* Iter for values which is uint64_t */
				size += (sizeof(u64));
			}
		}
	}

	return size;
}

/**
 * gcov_to_gcda - convert from gcov data set (info) to
 * .gcda file format.
 * This buffer will now have info similar to a regular gcda
 * format.
 */
size_t gcov_to_gcda(u8 *buffer, struct gcov_info *info)
{
	struct gcov_fn_info *functions;
	struct gcov_ctr_info *counters_per_func;
	u32 iter_functions;
	u32 iter_counts;
	u32 iter_counter_values;
	size_t buffer_write_position = 0;

	/* File header. */
	write_u32(buffer, &buffer_write_position, GCOV_DATA_MAGIC);

	write_u32(buffer, &buffer_write_position, info->version);

	write_u32(buffer, &buffer_write_position, info->stamp);

#ifdef GCOV_12_FORMAT
	write_u32(buffer, &buffer_write_position, info->checksum);
#endif

	for (iter_functions = 0U; iter_functions < info->n_functions;
	     iter_functions++) {
		functions = info->functions[iter_functions];

		write_u32(buffer, &buffer_write_position, GCOV_TAG_FUNCTION);

		write_u32(buffer, &buffer_write_position,
			  GCOV_TAG_FUNCTION_LENGTH);

		write_u32(buffer, &buffer_write_position, functions->ident);

		write_u32(buffer, &buffer_write_position,
			  functions->lineno_checksum);

		write_u32(buffer, &buffer_write_position,
			  functions->cfg_checksum);

		counters_per_func = functions->ctrs;

		for (iter_counts = 0U; iter_counts < GCOV_COUNTERS;
		     iter_counts++) {
			if (!info->merge[iter_counts]) {
				continue;
			}

			write_u32(buffer, &buffer_write_position,
				  GCOV_TAG_FOR_COUNTER(iter_counts));

#ifdef GCOV_12_FORMAT
			/* GCOV 12 counts the length by bytes */
			write_u32(buffer, &buffer_write_position,
				  counters_per_func->num * 2U * 4);
#else
			write_u32(buffer, &buffer_write_position,
				  counters_per_func->num * 2U);
#endif

			for (iter_counter_values = 0U;
			     iter_counter_values < counters_per_func->num;
			     iter_counter_values++) {
				write_u64(
					buffer, &buffer_write_position,
					counters_per_func
						->values[iter_counter_values]);
			}

			counters_per_func++;
		}
	}
	return buffer_write_position;
}

void dump_on_console_start(const char *filename)
{
	vmm_printf("\n%c", FILE_START_INDICATOR);
	while (*filename != '\0') {
		vmm_printf("%c", *filename++);
	}
	vmm_printf("%c", GCOV_DUMP_SEPARATOR);
}

void dump_on_console_data(char *ptr, size_t len)
{
	if (ptr != NULL) {
		for (size_t iter = 0U; iter < len; iter++) {
			print_u8((u8)*ptr++);
		}
	}
}

/**
 * Retrieves gcov coverage data and sends it over the given interface.
 */
void gcov_coverage_dump(void)
{
	u8 *buffer;
	size_t size;
	size_t written_size;
	struct gcov_info *gcov_list_first = gcov_info_head;
	struct gcov_info *gcov_list = gcov_info_head;

	vmm_spin_lock(&coverage_lock);
	vmm_printf("\nGCOV_COVERAGE_DUMP_START");
	while (gcov_list) {
		dump_on_console_start(gcov_list->filename);
		size = gcov_calculate_buff_size(gcov_list);

		buffer = vmm_malloc(size);
		if (!buffer) {
			vmm_printf("No Mem available to continue dump\n");
			goto coverage_dump_end;
		}

		written_size = gcov_to_gcda(buffer, gcov_list);
		if (written_size != size) {
			vmm_printf("Write Error on buff\n");
			goto coverage_dump_end;
		}

		dump_on_console_data((char *)buffer, size);

		vmm_free(buffer);
		gcov_list = gcov_list->next;
		if (gcov_list_first == gcov_list) {
			goto coverage_dump_end;
		}
	}
coverage_dump_end:
	vmm_printf("\nGCOV_COVERAGE_DUMP_END\n");
	vmm_spin_unlock(&coverage_lock);
	return;
}

struct gcov_info *gcov_get_list_head(void)
{
	/* Locking someway before getting this is recommended. */
	return gcov_info_head;
}

typedef unsigned long int uintptr_t;

/* Initialize the gcov by calling the required constructors */
void gcov_static_init(void)
{
	INIT_SPIN_LOCK(&coverage_lock);

	extern uintptr_t __init_array_start, __init_array_end;
	uintptr_t func_pointer_start = (uintptr_t)&__init_array_start;
	uintptr_t func_pointer_end = (uintptr_t)&__init_array_end;

	while (func_pointer_start < func_pointer_end) {
		void (**p)(void);
		/* get function pointer */
		p = (void (**)(void))func_pointer_start;
		(*p)();
		func_pointer_start += sizeof(p);
	}
}
