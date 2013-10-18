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

void lwt_yield();
void* lwt_join(lwt_t lwt);
void lwt_die(void *data);

#endif
