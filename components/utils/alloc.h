#ifndef INCLUDE_ALLOC_H
#define INCLUDE_ALLOC_H

void free(void *);

#define DEFINE_DEFER(name, type, free) \
	static inline void alloc_defer_##name(void *p) { type _T = *(type *)p; free;}

#define alloc_defer(name) __attribute__((__cleanup__(alloc_defer_##name)))

#define no_defer_ptr(p) \
	({ typeof(p) __ptr = (p); (p) = NULL; __ptr; })

DEFINE_DEFER(generic, void *, free(_T))

#define alloc_dfree alloc_defer(generic)
#define alloc_auto(type) alloc_defer(type) type

#endif
