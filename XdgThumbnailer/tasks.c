#include "tasks.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdbool.h>

#define URGENT_FLAG (1u << (sizeof(unsigned) * CHAR_BIT - 1)) // The compiler should warn if this shift is out of range.

struct Tasks {
    // Note that the lock only applies to the whole array; individual elements may be mutated
    // in-place just fine by the readers.
    GRWLock lock;
    GArray /* of GCancellable* */ *tasks;
};
static struct Tasks urgent_tasks, tasks;

static void init_task_list(struct Tasks *task_list)
{
    g_rw_lock_init(&task_list->lock);
    task_list->tasks = g_array_new(false, false, sizeof(GCancellable *));
}
void init_tasks(void)
{
    init_task_list(&urgent_tasks);
    init_task_list(&tasks);
}

static void cleanup_task_list(struct Tasks *task_list)
{
    // TODO: wait for the remaining tasks to end?
    g_rw_lock_clear(&task_list->lock);
    g_array_unref(task_list->tasks);
}
void cleanup_tasks(void)
{
    cleanup_task_list(&urgent_tasks);
    cleanup_task_list(&tasks);
}

struct NewTaskInfo new_task(gboolean is_urgent)
{
    struct Tasks *task_list = is_urgent ? &urgent_tasks : &tasks;
    GCancellable **array = (void *)task_list->tasks->data;

    GCancellable *cancellable = g_cancellable_new();

    // We may reallocate the array, so we need a writer lock.
    g_rw_lock_writer_lock(&task_list->lock);
    // First, look for a free slot in the array.
    unsigned index = 0;
    for (unsigned i = 0; i < task_list->tasks->len; ++i) {
        if (array[i] == NULL) {
            array[i] = cancellable;
            index = i + 1;
            goto got_slot;
        }
    }
    // We need to allocate a new slot.

    // Each task list cannot contain 0x7FFFFFFF handles, as otherwise bit 7 cannot differentiate
    // between regular and urgent tasks.
    // Note that index 0 is invalid, since it's reserved for "no handle", so that's 1 less.
    if (task_list->tasks->len == URGENT_FLAG - 2) {
        g_object_unref(cancellable);
        return (struct NewTaskInfo){.handle = 0};
    }
    g_array_append_val(task_list->tasks, cancellable);
    index = task_list->tasks->len; // We want the new index *plus one*.
got_slot:
    g_rw_lock_writer_unlock(&task_list->lock);

    g_assert_cmpuint(index, !=, 0);
    g_assert_cmpuint(index, <, URGENT_FLAG);

    return (struct NewTaskInfo){.handle = is_urgent ? (index | URGENT_FLAG) : index, .cancellable = cancellable};
}

void cancel_task(unsigned handle)
{
    struct Tasks *task_list = (handle & URGENT_FLAG) ? &urgent_tasks : &tasks;

    g_rw_lock_reader_lock(&task_list->lock);
    GCancellable **slot = &((GCancellable **)task_list->tasks->data)[(handle & ~URGENT_FLAG) - 1];
    GCancellable *cancellable = *slot;
    *slot = NULL;
    g_rw_lock_reader_unlock(&task_list->lock);

    g_cancellable_cancel(cancellable);
    g_object_unref(cancellable);
}

void finished_task(unsigned handle)
{
    struct Tasks *task_list = (handle & URGENT_FLAG) ? &urgent_tasks : &tasks;

    g_rw_lock_reader_lock(&task_list->lock);
    GCancellable **slot = &((GCancellable **)task_list->tasks->data)[(handle & ~URGENT_FLAG) - 1];
    GCancellable *cancellable = *slot;
    *slot = NULL;
    g_rw_lock_reader_unlock(&task_list->lock);

    g_object_unref(cancellable);
}
