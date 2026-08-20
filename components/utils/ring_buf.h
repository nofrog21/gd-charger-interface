#pragma once

inline size_t ring_buf_cnt(int head, int tail, size_t size)
{
	return (head - tail) & (size - 1);
}

inline size_t ring_buf_space(int head, int tail, size_t size)
{
	return (tail - head - 1) & (size - 1);
}

inline size_t ring_buf_cnt_to_end(int head, int tail, size_t size)
{
	size_t end = size - tail;
	size_t n = (head + end) & (size - 1);
	return n < end ? n : end;
}

inline size_t ring_buf_space_to_end(int head, int tail, size_t size)
{
	size_t end = size - 1 - head;
	size_t n = (tail + end) & (size - 1);
	return n <= end ? n : end + 1;
}
