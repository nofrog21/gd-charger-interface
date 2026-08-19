#ifndef INCLUDE_LIST
#define INCLUDE_LIST

#include <stddef.h>

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

static inline void INIT_LIST_HEAD(struct list_head *h)
{
	h->next = h;
	h->prev = h;
}

#define LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

static inline void __list_add(struct list_head *new,
    struct list_head *prev,
    struct list_head *next)
{
	new->prev = prev;
	new->next = next;
	prev->next = new;
	next->prev = new;
}

static inline void __list_del(struct list_head *prev,
    struct list_head *next)
{
	prev->next = next;
	next->prev = prev;
}

static inline void list_add_tail(struct list_head *head, struct list_head *new)
{
	__list_add(new, head->prev, head);
}

static inline void list_add(struct list_head *head, struct list_head *new)
{
	__list_add(new, head, head->next);
}

static inline void list_del(struct list_head *node)
{
	__list_del(node->prev, node->next);
	node->next = NULL;
	node->prev = NULL;
}

#ifndef container_of
#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

#define list_foreach(pos, head) \
	for (struct list_head *pos = (head)->next; pos != (head); pos = pos->next)

#endif
