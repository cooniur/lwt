#ifndef lwt_h
#define lwt_h

/**
 lwt_fn_t: Type of a pointer to a thread entry function
 */
typedef void*(*lwt_fn_t)(void*);

/**
 lwt_t: Type of a pointer to a thread descriptor defined by __lwt_t__ in .c file
 */
typedef struct __lwt_t__ *lwt_t;

typedef enum __lwt_status_t__ lwt_status_t;

lwt_t lwt_create(lwt_fn_t fn, void *data);

void test_inline_as(lwt_t lwt);

#endif
