/* bench.c */
int main(int argc, char *argv[]);
unsigned long array_test(unsigned long array_size, int reps, int wsize);
unsigned long write_test_low(uint64_t file_size);
unsigned long seek_test(uint64_t file_size);
unsigned long read_test_low(uint64_t file_size);
char *size_str(uint64_t size, char *str, char *suffix);
void report_throughput(unsigned long milliseconds, char *tag, uint64_t data_size, int reps, unsigned long block_size);
void report_random(unsigned long milliseconds, char *tag, unsigned long block_size);
void empty_bar(int width);
void progress_bar(unsigned long c, unsigned long max_c, unsigned long width);
time_t difftimeofday(struct timeval *start_time, struct timeval *end_time);
