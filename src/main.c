#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#define ROUND_SIZE_UP_TO_ALIGN(size, align) (((size + align - 1) / align) * align)
#define ROUND_SIZE_UP_TO_MAX_ALIGN(size) ROUND_SIZE_UP_TO_ALIGN(size, _Alignof(max_align_t))

#define MIN(a, b) (a < b ? a : b)
#define SIZE(a) (sizeof(a) / sizeof(a[0]))

#define ITERATOR_NEXT(obj) _Generic((obj), \
    VecIter *: VecIter_next,               \
    AdapterIter *: AdapterIter_next,       \
    Iterator *: Iterator_next)

#define EACH_IN(e, iter)                           \
    void *e = (void *)ITERATOR_NEXT(&iter)(&iter); \
    e != NULL;                                     \
    e = (void *)ITERATOR_NEXT(&iter)(&iter)

typedef void (*DropFn)(void *this);
typedef int_fast8_t (*CmpFn)(const void *a, const void *b);
typedef bool (*EqFn)(const void *a, const void *b);

// [MaybeOwned]

typedef enum
{
    MAYBE_OWNED_KIND_OWNED,
    MAYBE_OWNED_KIND_BORROWED,
} MaybeOwnedKind;

typedef struct
{
    size_t size;
    DropFn drop;
} MaybeOwnedOwnedProps;

typedef struct
{
    MaybeOwnedKind kind;

    union
    {
        struct
        {
            void *value;
            MaybeOwnedOwnedProps props;
        } owned;

        struct
        {
            void *value;
        } borrowed;
    };
} MaybeOwned;

MaybeOwned MaybeOwned_owned(void *value, const MaybeOwnedOwnedProps *props)
{
    MaybeOwned this;

    this.kind = MAYBE_OWNED_KIND_OWNED;
    this.owned.props = *props;

    this.owned.value = malloc(this.owned.props.size);
    memcpy(this.owned.value, value, this.owned.props.size);

    return this;
}

MaybeOwned MaybeOwned_borrowed(void *value)
{
    MaybeOwned this;

    this.kind = MAYBE_OWNED_KIND_BORROWED;
    this.borrowed.value = value;

    return this;
}

void *MaybeOwned_value(const MaybeOwned *this)
{
    return this->kind == MAYBE_OWNED_KIND_BORROWED ? this->borrowed.value : this->owned.value;
}

void MaybeOwned_drop(MaybeOwned *this)
{
    if (this->kind == MAYBE_OWNED_KIND_OWNED)
    {
        if (this->owned.props.drop != NULL)
        {
            this->owned.props.drop(this->owned.value);
        }

        free(this->owned.value);
    }
}

// [Iterator]

typedef void *(*IteratorNextFn)(void *this);
typedef void *(*IteratorNextBackFn)(void *this);
typedef size_t (*IteratorLenFn)(const void *this);

typedef enum
{
    ITERATOR_CAPABILITY_ITERATOR = 1,
    ITERATOR_CAPABILITY_DOUBLE_ENDED_ITERATOR = 1 << 1,
    ITERATOR_CAPABILITY_EXACT_SIZE_ITERATOR = 1 << 2,
} IteratorCapability;

typedef struct
{
    IteratorNextFn next;
    IteratorNextBackFn next_back;
    IteratorLenFn len;
} IteratorProps;

typedef struct
{
    void *concrete;
    IteratorCapability capabilities;
    IteratorProps props;
} Iterator;

Iterator Iterator_new(void *concrete, const IteratorProps *props)
{
    IteratorCapability capabilities = ITERATOR_CAPABILITY_ITERATOR;
    capabilities |= props->next_back != NULL ? ITERATOR_CAPABILITY_DOUBLE_ENDED_ITERATOR : 0;
    capabilities |= props->len != NULL ? ITERATOR_CAPABILITY_EXACT_SIZE_ITERATOR : 0;

    return (Iterator){
        .concrete = concrete,
        .props = *props,
        .capabilities = capabilities,
    };
}

void *Iterator_next(Iterator *this)
{
    assert(this->capabilities & ITERATOR_CAPABILITY_ITERATOR);

    return this->props.next(this->concrete);
}

static void _Iterator_advance_by(Iterator *this, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (Iterator_next(this) == NULL)
        {
            break;
        }
    }
}

void *Iterator_nth(Iterator *this, size_t n)
{
    assert(this->capabilities & ITERATOR_CAPABILITY_ITERATOR);

    _Iterator_advance_by(this, n);
    return Iterator_next(this);
}

void *Iterator_next_back(Iterator *this)
{
    assert(this->capabilities & ITERATOR_CAPABILITY_DOUBLE_ENDED_ITERATOR);

    return this->props.next_back(this->concrete);
}

static void _Iterator_advance_back_by(Iterator *this, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (Iterator_next_back(this) == NULL)
        {
            break;
        }
    }
}

void *Iterator_nth_back(Iterator *this, size_t n)
{
    assert(this->capabilities & ITERATOR_CAPABILITY_DOUBLE_ENDED_ITERATOR);

    _Iterator_advance_back_by(this, n);
    return Iterator_next_back(this);
}

size_t Iterator_len(const Iterator *this)
{
    assert(this->capabilities & ITERATOR_CAPABILITY_EXACT_SIZE_ITERATOR);

    return this->props.len(this->concrete);
}

bool Iterator_is_empty(const Iterator *this)
{
    assert(this->capabilities & ITERATOR_CAPABILITY_EXACT_SIZE_ITERATOR);

    return Iterator_len(this) == 0;
}

void Iterator_drop(Iterator *this)
{
}

// [LinkedIterator]

typedef struct
{
    Iterator iter;
    DropFn drop;
} LinkedIterator;

// [SkipIter]

typedef struct
{
    Iterator concrete;
    size_t n;
} SkipIter;

void SkipIter_new(SkipIter *this, Iterator *concrete, size_t n)
{
    this->concrete = *concrete;
    this->n = n;
}

void *SkipIter_next(SkipIter *this)
{
    if (this->n != 0)
    {
        size_t n = this->n;
        this->n = 0;
        return Iterator_nth(&this->concrete, n);
    }
    else
    {
        return Iterator_next(&this->concrete);
    }
}

size_t SkipIter_len(const SkipIter *this)
{
    size_t len = Iterator_len(&this->concrete);

    if (this->n > len)
    {
        return 0;
    }
    else
    {
        return len - this->n;
    }
}

void *SkipIter_next_back(SkipIter *this)
{
    return SkipIter_len(this) == 0 ? NULL : Iterator_next_back(&this->concrete);
}

void SkipIter_drop(SkipIter *this)
{
    Iterator_drop(&this->concrete);
}

// [TakeIter]

typedef struct
{
    Iterator concrete;
    size_t n;
} TakeIter;

void TakeIter_new(TakeIter *this, Iterator *concrete, size_t n)
{
    this->concrete = *concrete;
    this->n = n;
}

void *TakeIter_next(TakeIter *this)
{
    if (this->n == 0)
    {
        return NULL;
    }
    else
    {
        this->n -= 1;

        return Iterator_next(&this->concrete);
    }
}

void *TakeIter_next_back(TakeIter *this)
{
    size_t len = Iterator_len(&this->concrete);

    if (this->n > len)
    {
        return Iterator_nth_back(&this->concrete, 0);
    }
    else
    {
        this->n -= 1;
        return Iterator_nth_back(&this->concrete, len - this->n);
    }
}

size_t TakeIter_len(const TakeIter *this)
{
    size_t len = Iterator_len(&this->concrete);
    return MIN(len, this->n);
}

void TakeIter_drop(TakeIter *this)
{
    Iterator_drop(&this->concrete);
}

// [StepByIter]

typedef struct
{
    Iterator concrete;
    size_t step;
    bool first_take;
} StepByIter;

void StepByIter_new(StepByIter *this, Iterator *concrete, size_t step)
{
    this->concrete = *concrete;
    this->step = step;
    this->first_take = true;
}

void *StepByIter_next(StepByIter *this)
{
    size_t n = this->first_take ? 0 : this->step - 1;
    this->first_take = false;
    return Iterator_nth(&this->concrete, n);
}

static size_t _StepByIter_next_back_nth(const StepByIter *this)
{
    size_t len = Iterator_len(&this->concrete);
    size_t last_idx = this->first_take ? len - 1 : len;
    return last_idx % this->step;
}

void *StepByIter_next_back(StepByIter *this)
{
    size_t len = Iterator_len(&this->concrete);

    if (len == 0)
    {
        return NULL;
    }
    else
    {
        return Iterator_nth_back(&this->concrete, _StepByIter_next_back_nth(this));
    }
}

size_t StepByIter_len(const StepByIter *this)
{
    size_t len = Iterator_len(&this->concrete);

    if (len == 0)
    {
        return 0;
    }

    if (this->first_take)
    {
        return 1 + (len - 1) / this->step;
    }
    else
    {
        return len / this->step;
    }
}

void StepByIter_drop(StepByIter *this)
{
    Iterator_drop(&this->concrete);
}

// [RevIter]

typedef struct
{
    Iterator concrete;
} RevIter;

void RevIter_new(RevIter *this, Iterator *concrete)
{
    this->concrete = *concrete;
}

void *RevIter_next(RevIter *this)
{
    return Iterator_next_back(&this->concrete);
}

void *RevIter_next_back(RevIter *this)
{
    return Iterator_next(&this->concrete);
}

size_t RevIter_len(const RevIter *this)
{
    return Iterator_len(&this->concrete);
}

void RevIter_drop(RevIter *this)
{
    Iterator_drop(&this->concrete);
}

// [AdapterIter]

#define ADAPTER_ITER_NEW(base, ...) AdapterIter_new( \
    METHOD(&base, linked_iter)(&base),               \
    (AdapterIterSpec[]){                             \
        __VA_ARGS__,                                 \
        AdapterIterSpec_none(),                      \
    });

typedef struct AdapterIterSpec AdapterIterSpec;

typedef Iterator (*AdapterIterNewFn)(void *this, Iterator *concrete, const AdapterIterSpec *spec);

typedef struct
{
    size_t size;
    AdapterIterNewFn new;
    DropFn drop;
} AdapterIterSpecProps;

struct AdapterIterSpec
{
    AdapterIterSpecProps props;
    bool is_none;

    union
    {
        struct
        {
            size_t n;
        } skip;

        struct
        {
            size_t n;
        } take;

        struct
        {
            size_t step;
        } step_by;
    };
};

static Iterator _AdapterIterSpec_new_skip(SkipIter *this, Iterator *concrete, const AdapterIterSpec *spec)
{
    SkipIter_new(this, concrete, spec->skip.n);

    return Iterator_new(
        this,
        &(IteratorProps){
            .next = (IteratorNextFn)SkipIter_next,
            .next_back = (IteratorNextBackFn)SkipIter_next_back,
            .len = (IteratorLenFn)SkipIter_len,
        });
}

AdapterIterSpec AdapterIterSpec_skip(size_t n)
{
    return (AdapterIterSpec){
        .props = {
            .size = sizeof(SkipIter),
            .new = (AdapterIterNewFn)_AdapterIterSpec_new_skip,
            .drop = (DropFn)SkipIter_drop,
        },
        .skip = {
            .n = n,
        },
    };
}

static Iterator _AdapterIterSpec_new_take(TakeIter *this, Iterator *concrete, const AdapterIterSpec *spec)
{
    TakeIter_new(this, concrete, spec->take.n);

    return Iterator_new(
        this,
        &(IteratorProps){
            .next = (IteratorNextFn)TakeIter_next,
            .next_back = (IteratorNextBackFn)TakeIter_next_back,
            .len = (IteratorLenFn)TakeIter_len,
        });
}

AdapterIterSpec AdapterIterSpec_take(size_t n)
{
    return (AdapterIterSpec){
        .props = {
            .size = sizeof(TakeIter),
            .new = (AdapterIterNewFn)_AdapterIterSpec_new_take,
            .drop = (DropFn)TakeIter_drop,
        },
        .take = {
            .n = n,
        },
    };
}

static Iterator _AdapterIterSpec_new_step_by(StepByIter *this, Iterator *concrete, const AdapterIterSpec *spec)
{
    StepByIter_new(this, concrete, spec->step_by.step);

    return Iterator_new(
        this,
        &(IteratorProps){
            .next = (IteratorNextFn)StepByIter_next,
            .next_back = (IteratorNextBackFn)StepByIter_next_back,
            .len = (IteratorLenFn)StepByIter_len,
        });
}

AdapterIterSpec AdapterIterSpec_step_by(size_t step)
{
    return (AdapterIterSpec){
        .props = {
            .size = sizeof(StepByIter),
            .new = (AdapterIterNewFn)_AdapterIterSpec_new_step_by,
            .drop = (DropFn)StepByIter_drop,
        },
        .step_by = {
            .step = step,
        },
    };
}

static Iterator _AdapterIterSpec_new_rev(RevIter *this, Iterator *concrete, const AdapterIterSpec *spec)
{
    (void)spec;

    RevIter_new(this, concrete);

    return Iterator_new(
        this,
        &(IteratorProps){
            .next = (IteratorNextFn)RevIter_next,
            .next_back = (IteratorNextBackFn)RevIter_next_back,
            .len = (IteratorLenFn)RevIter_len,
        });
}

AdapterIterSpec AdapterIterSpec_rev()
{
    return (AdapterIterSpec){
        .props = {
            .size = sizeof(RevIter),
            .new = (AdapterIterNewFn)_AdapterIterSpec_new_rev,
            .drop = (DropFn)RevIter_drop,
        },
    };
}

AdapterIterSpec AdapterIterSpec_none()
{
    return (AdapterIterSpec){
        .is_none = true,
    };
}

typedef struct
{
    Iterator concrete;
    LinkedIterator base;
    void *buffer;
    size_t length;
} AdapterIter;

AdapterIter AdapterIter_new(LinkedIterator concrete, const AdapterIterSpec *adapters)
{
    size_t size = 0;
    size_t adapter_count = 0;

    for (const AdapterIterSpec *current = adapters; !current->is_none; current++)
    {
        size += ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(current->props)) + ROUND_SIZE_UP_TO_MAX_ALIGN(current->props.size);
        adapter_count++;
    }

    void *buffer = malloc(size);
    uint8_t *current = buffer;

    Iterator previous_iter = concrete.iter;

    for (size_t i = 0; i < adapter_count; i++)
    {
        *(AdapterIterSpecProps *)(current) = adapters[i].props;
        current += ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(adapters[i].props));

        previous_iter = adapters[i].props.new(current, &previous_iter, adapters + i);

        current += ROUND_SIZE_UP_TO_MAX_ALIGN(adapters[i].props.size);
    }

    return (AdapterIter){
        .concrete = previous_iter,
        .base = concrete,
        .buffer = buffer,
        .length = adapter_count,
    };
}

void *AdapterIter_next(AdapterIter *this)
{
    return Iterator_next(&this->concrete);
}

size_t AdapterIter_len(const AdapterIter *this)
{
    return Iterator_len(&this->concrete);
}

void *AdapterIter_next_back(AdapterIter *this)
{
    return Iterator_next_back(&this->concrete);
}

Iterator AdapterIter_iter(AdapterIter *this)
{
    return Iterator_new(
        this,
        &(IteratorProps){
            .next = (IteratorNextFn)AdapterIter_next,
            .next_back = (IteratorNextBackFn)AdapterIter_next_back,
            .len = (IteratorLenFn)AdapterIter_len,
        });
}

void AdapterIter_drop(AdapterIter *this)
{
    if (this->base.drop != NULL)
    {
        this->base.drop(&this->base.iter.concrete);
    }

    uint8_t *current = this->buffer;

    for (size_t i = 0; i < this->length; i++)
    {
        AdapterIterSpecProps props = *(AdapterIterSpecProps *)(current);
        current += ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(props));

        if (props.drop != NULL)
        {
            props.drop(current);
        }
    }

    free(this->buffer);
}

LinkedIterator AdapterIter_linked_iter(AdapterIter *this)
{
    return (LinkedIterator){
        .iter = AdapterIter_iter(this),
        .drop = (DropFn)AdapterIter_drop,
    };
}

// [Vec]

typedef struct
{
    DropFn drop;
} VecElementOps;

typedef struct
{
    size_t length;
    size_t capacity;
    size_t element_size;
    void *data;
    VecElementOps element_ops;
} Vec;

static void _Vec_drop_element(Vec *this, void *element)
{
    if (this->element_ops.drop)
    {
        this->element_ops.drop(element);
    }
    // If this->element_ops.drop == NULL, assume element is POD and doesn't need to be dropped
}

void Vec_new(Vec *this, size_t element_size, const VecElementOps *element_ops)
{
    this->element_size = element_size;

    if (element_ops == NULL)
    {
        this->element_ops.drop = NULL;
    }
    else
    {
        this->element_ops = *element_ops;
    }

    this->length = 0;
    this->capacity = 0;
    this->data = NULL;
}

void Vec_with_capacity(Vec *this, size_t element_size, const VecElementOps *element_ops, size_t capacity)
{
    Vec_new(this, element_size, element_ops);

    this->data = malloc(capacity * element_size);
    this->capacity = capacity;
}

size_t Vec_len(const Vec *this)
{
    return this->length;
}

void Vec_set_len(Vec *this, size_t len)
{
    this->length = len;
}

size_t Vec_capacity(Vec *this)
{
    return this->capacity;
}

const void *Vec_get(const Vec *this, size_t i)
{
    return (uint8_t *)(this->data) + (i * this->element_size);
}

void *Vec_get_mut(Vec *this, size_t i)
{
    return (void *)Vec_get(this, i);
}

void Vec_push(Vec *this, const void *value)
{
    if (this->length == this->capacity)
    {
        size_t new_capacity = this->capacity ? this->capacity * 2 : 10;

        this->data = realloc(this->data, new_capacity * this->element_size);
        this->capacity = new_capacity;
    }

    memcpy(Vec_get_mut(this, this->length), value, this->element_size);

    this->length++;
}

void Vec_pop(Vec *this)
{
    if (this->length != 0)
    {
        _Vec_drop_element(this, Vec_get_mut(this, this->length - 1));

        this->length--;
    }
}

void Vec_insert(Vec *this, size_t i, const void *value)
{
    if (this->length == this->capacity)
    {
        size_t new_capacity = this->capacity ? this->capacity * 2 : 10;

        this->data = realloc(this->data, new_capacity * this->element_size);
        this->capacity = new_capacity;
    }

    if (i == this->length)
    {
        Vec_push(this, value);
    }
    else if (i < this->length)
    {
        void *new_element_address = Vec_get_mut(this, i);

        const size_t elements_to_move = this->length - i;
        memmove(Vec_get_mut(this, i + 1), new_element_address, elements_to_move * this->element_size);

        memcpy(new_element_address, value, this->element_size);

        this->length++;
    }
}

void Vec_remove(Vec *this, size_t i)
{
    if (i == (this->length - 1))
    {
        Vec_pop(this);
    }
    else if (i < (this->length - 1))
    {
        void *element = Vec_get_mut(this, i);

        _Vec_drop_element(this, element);

        const size_t elements_to_move = this->length - i - 1;
        memmove(element, Vec_get(this, i + 1), elements_to_move * this->element_size);

        this->length--;
    }
}

void Vec_reserve(Vec *this, size_t additional)
{
    size_t required = this->length + additional;

    if (this->capacity < required)
    {
        this->data = realloc(this->data, required * this->element_size);
        this->capacity = required;
    }
}

void Vec_truncate(Vec *this, size_t len)
{
    if (len < this->length)
    {
        for (size_t i = len; i < this->length; i++)
        {
            _Vec_drop_element(this, Vec_get_mut(this, i));
        }

        this->length = len;
    }
}

void Vec_clear(Vec *this)
{
    Vec_truncate(this, 0);
}

void Vec_shrink_to_fit(Vec *this)
{
    if (this->length == this->capacity)
    {
        return;
    }

    if (this->length == 0)
    {
        free(this->data);
        this->data = NULL;
        this->capacity = 0;
    }
    else
    {
        this->data = realloc(this->data, this->length * this->element_size);
        this->capacity = this->length;
    }
}

void Vec_drop(Vec *this)
{
    Vec_clear(this);

    free(this->data);
    this->data = NULL;
}

// [VecIter]

typedef struct
{
    const Vec *vec;
    size_t start;
    size_t end;
    bool is_done;
} VecIter;

void VecIter_new(VecIter *this, const Vec *vec)
{
    this->vec = vec;

    if (Vec_len(vec) == 0)
    {
        this->is_done = true;
    }
    else
    {
        this->is_done = false;
        this->start = 0;
        this->end = Vec_len(vec) - 1;
    }
}

const void *VecIter_next(VecIter *this)
{
    if (this->is_done)
    {
        return NULL;
    }

    this->is_done = this->start == this->end;

    const void *element = Vec_get(this->vec, this->start);
    this->start += 1;
    return element;
}

const void *VecIter_next_back(VecIter *this)
{
    if (this->is_done)
    {
        return NULL;
    }

    this->is_done = this->start == this->end;

    const void *element = Vec_get(this->vec, this->end);
    this->end -= 1;
    return element;
}

size_t VecIter_len(const VecIter *this)
{
    return this->end - this->start + 1;
}

void VecIter_drop(VecIter *this)
{
}

VecIter Vec_iter(const Vec *this)
{
    VecIter iter;
    VecIter_new(&iter, this);

    return iter;
}

Iterator VecIter_iter(VecIter *this)
{
    return Iterator_new(
        this,
        &(IteratorProps){
            .next = (IteratorNextFn)VecIter_next,
            .next_back = (IteratorNextBackFn)VecIter_next_back,
            .len = (IteratorLenFn)VecIter_len,
        });
}

LinkedIterator VecIter_linked_iter(VecIter *this)
{
    return (LinkedIterator){
        .iter = VecIter_iter(this),
        .drop = (DropFn)VecIter_drop,
    };
}

// [RangeBound]

typedef enum
{
    RANGE_BOUND_KIND_INCLUDED,
    RANGE_BOUND_KIND_EXCLUDED,
    RANGE_BOUND_KIND_UNBOUND,
} RangeBoundKind;

typedef struct
{
    RangeBoundKind kind;
    union
    {
        struct
        {
            const void *value;
        } included;

        struct
        {
            const void *value;
        } excluded;
    };
} RangeBound;

RangeBound RangeBound_included(void *value)
{
    RangeBound this = {
        .kind = RANGE_BOUND_KIND_INCLUDED,
        .included.value = value,
    };
    return this;
}

RangeBound RangeBound_excluded(void *value)
{
    RangeBound this = {
        .kind = RANGE_BOUND_KIND_EXCLUDED,
        .excluded.value = value,
    };
    return this;
}

RangeBound RangeBound_unbound(void *value)
{
    RangeBound this = {
        .kind = RANGE_BOUND_KIND_UNBOUND,
    };
    return this;
}

// [BTreeMap]

#define BTREEMAP_T 3
#define BTREEMAP_MINIMUM_KEY_COUNT (BTREEMAP_T - 1)
#define BTREEMAP_MAXIMUM_KEY_COUNT (2 * BTREEMAP_T - 1)
#define BTREEMAP_MINIMUM_CHILD_COUNT (BTREEMAP_MINIMUM_KEY_COUNT + 1)
#define BTREEMAP_MAXIMUM_CHILD_COUNT (BTREEMAP_MAXIMUM_KEY_COUNT + 1)

static void _swap(void *a, void *b, size_t size)
{
    uint8_t tmp[size];

    memcpy(tmp, a, size);
    memcpy(a, b, size);
    memcpy(b, tmp, size);
}

#define SWAP(a, b) _swap(&a, &b, sizeof(a))

typedef struct __BTreeMapNode
{
    struct __BTreeMapNode *parent;
    bool is_leaf;
    struct __BTreeMapNode *children[BTREEMAP_MAXIMUM_CHILD_COUNT];
    size_t key_count;
} _BTreeMapNode;

typedef struct
{
    _BTreeMapNode *node;
    size_t child_idx;
} BTreeMapChildPos;

static void _BTreeMapNode_new(_BTreeMapNode *this, const _BTreeMapNode *parent, bool is_leaf)
{
    this->parent = (_BTreeMapNode *)parent;
    this->is_leaf = is_leaf;
    this->key_count = 0;
}

static BTreeMapChildPos _BTreeMapNode_child_pos(const _BTreeMapNode *this, _BTreeMapNode *child)
{
    size_t child_idx = SIZE_MAX;

    for (size_t i = 0; i < this->key_count + 1; i++)
    {
        if (this->children[i] == child)
        {
            child_idx = i;
            break;
        }
    }

    BTreeMapChildPos result = {
        .node = (_BTreeMapNode *)this,
        .child_idx = child_idx,
    };
    return result;
}

typedef struct
{
    size_t size;
    CmpFn cmp;
    DropFn drop;
} BTreeMapKeyProps;

typedef struct
{
    size_t size;
    DropFn drop;
} BTreeMapValueProps;

typedef struct
{
    _BTreeMapNode *root;
    size_t length;
    BTreeMapKeyProps key_props;
    BTreeMapValueProps value_props;
} BTreeMap;

typedef struct
{
    void *key;
    void *value;
} BTreeMapEntry;

BTreeMapChildPos BTreeMapChildPos_new(_BTreeMapNode *node, size_t child_idx)
{
    BTreeMapChildPos this = {
        .node = node,
        .child_idx = child_idx,
    };
    return this;
}

_BTreeMapNode *BTreeMapChildPos_to_child(BTreeMapChildPos this)
{
    return this.node->children[this.child_idx];
}

BTreeMapEntry _BTreeMapEntry_new(void *key, void *value)
{
    BTreeMapEntry this = {
        .key = key,
        .value = value,
    };
    return this;
}

typedef struct
{
    _BTreeMapNode *node;
    size_t kv_idx;
} BTreeMapEntryPos;

BTreeMapEntryPos BTreeMapEntryPos_new(_BTreeMapNode *node, size_t kv_idx)
{
    BTreeMapEntryPos this = {
        .node = node,
        .kv_idx = kv_idx,
    };
    return this;
}

BTreeMapEntry BTreeMapEntryPos_to_entry(BTreeMapEntryPos this, const BTreeMap *map)
{
    uint8_t *keys = (uint8_t *)(this.node) + ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(*this.node));
    uint8_t *values = keys + ROUND_SIZE_UP_TO_MAX_ALIGN(BTREEMAP_MAXIMUM_KEY_COUNT * map->key_props.size);

    BTreeMapEntry result = {
        .key = keys + (this.kv_idx * map->key_props.size),
        .value = values + (this.kv_idx * map->value_props.size),
    };
    return result;
}

void _BTreeMap_move_entries(const BTreeMap *this, BTreeMapEntryPos from, BTreeMapEntryPos to)
{
    const size_t to_move = from.node->key_count - from.kv_idx;

    if (to_move)
    {
        BTreeMapEntry src = BTreeMapEntryPos_to_entry(from, this);
        BTreeMapEntry dest = BTreeMapEntryPos_to_entry(to, this);

        memmove(dest.key, src.key, to_move * this->key_props.size);
        memmove(dest.value, src.value, to_move * this->value_props.size);
    }
}

void _BTreeMap_move_children(BTreeMapChildPos from, BTreeMapChildPos to)
{
    const size_t to_move = (from.node->key_count + 1) - from.child_idx;

    if (to_move)
    {
        memmove(&from.node[from.child_idx], &to.node[to.child_idx], to_move * sizeof(from.node[0]));

        if (!from.node->is_leaf && from.node != to.node)
        {
            for (size_t i = 0; i < to_move; i++)
            {
                from.node->children[from.child_idx + i]->parent = to.node;
            }
        }
    }
}

void _BTreeMap_replace_entry_at(const BTreeMap *this, const BTreeMapEntry *new, const BTreeMapEntryPos *pos)
{
    BTreeMapEntry old = BTreeMapEntryPos_to_entry(*pos, this);

    memcpy(old.key, new->key, this->key_props.size);
    memcpy(old.value, new->value, this->value_props.size);
}

void _BTreeMap_drop_entry_at(const BTreeMap *this, const BTreeMapEntryPos *pos)
{
    BTreeMapEntry entry = BTreeMapEntryPos_to_entry(*pos, this);

    if (this->key_props.drop != NULL)
    {
        this->key_props.drop(entry.key);
    }

    if (this->value_props.drop != NULL)
    {
        this->value_props.drop(entry.value);
    }
}

void _BTreeMap_insert_entry_at(const BTreeMap *this, const BTreeMapEntry *entry, const BTreeMapEntryPos *pos)
{
    BTreeMapEntryPos from_pos = *pos;

    BTreeMapEntryPos to_pos = {
        .node = from_pos.node,
        .kv_idx = from_pos.kv_idx + 1,
    };

    _BTreeMap_move_entries(this, from_pos, to_pos);

    _BTreeMap_replace_entry_at(this, entry, pos);

    from_pos.node->key_count++;
}

void _BTreeMap_insert_child_at(const BTreeMap *this, const _BTreeMapNode *child, BTreeMapChildPos pos)
{
    BTreeMapChildPos from_pos = pos;

    BTreeMapChildPos to_pos = {
        .node = from_pos.node,
        .child_idx = from_pos.child_idx + 1,
    };

    _BTreeMap_move_children(from_pos, to_pos);

    pos.node->children[pos.child_idx] = (_BTreeMapNode *)child;
}

void _BTreeMap_remove_entry_at(const BTreeMap *this, const BTreeMapEntryPos *pos)
{
    BTreeMapEntryPos from_pos = {
        .node = pos->node,
        .kv_idx = pos->kv_idx + 1,
    };

    BTreeMapEntryPos to_pos = *pos;

    _BTreeMap_move_entries(this, from_pos, to_pos);

    from_pos.node->key_count--;
}

void _BTreeMap_remove_child_at(BTreeMapChildPos pos)
{
    BTreeMapChildPos from_pos = {
        .node = pos.node,
        .child_idx = pos.child_idx + 1,
    };

    BTreeMapChildPos to_pos = pos;

    _BTreeMap_move_children(from_pos, to_pos);
}

void _BTreeMap_split(const BTreeMap *this, _BTreeMapNode *left, _BTreeMapNode *right, BTreeMapEntryPos *separator_pos)
{
    _BTreeMapNode_new(right, left->parent, left->is_leaf);

    size_t separator_idx = left->key_count / 2;

    *separator_pos = BTreeMapEntryPos_new(left, separator_idx);

    _BTreeMap_move_entries(this, BTreeMapEntryPos_new(left, separator_idx + 1), BTreeMapEntryPos_new(right, 0));

    if (!left->is_leaf)
    {
        _BTreeMap_move_children(BTreeMapChildPos_new(left, separator_idx + 1), BTreeMapChildPos_new(right, 0));
    }

    right->key_count = left->key_count - (separator_idx + 1);
    left->key_count -= right->key_count;
}

static size_t _BTreeMap_node_size(const BTreeMap *this)
{
    return ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(_BTreeMapNode)) + ROUND_SIZE_UP_TO_MAX_ALIGN(BTREEMAP_MAXIMUM_KEY_COUNT * this->key_props.size) + ROUND_SIZE_UP_TO_MAX_ALIGN(BTREEMAP_MAXIMUM_KEY_COUNT * this->value_props.size);
}

static _BTreeMapNode *_BTreeMap_successor(const BTreeMap *this, const BTreeMapEntryPos *entry)
{
    _BTreeMapNode *current = entry->node->children[entry->kv_idx + 1];

    while (!current->is_leaf)
    {
        current = current->children[0];
    }

    return current;
}

static BTreeMapEntryPos _BTreeMap_predecessor(const BTreeMap *this, const BTreeMapEntryPos *entry)
{
    if (entry->node->is_leaf)
    {
        BTreeMapEntryPos result = {
            .node = NULL,
        };
        return result;
    }

    _BTreeMapNode *current = entry->node->children[entry->kv_idx];

    while (!current->is_leaf)
    {
        current = current->children[current->key_count - 1];
    }

    BTreeMapEntryPos result = {
        .node = current,
        .kv_idx = current->key_count - 1,
    };
    return result;
}

static _BTreeMapNode *_BTreeMap_rightmost(const BTreeMap *this)
{
    _BTreeMapNode *current = this->root;

    while (!current->is_leaf)
    {
        current = current->children[current->key_count];
    }

    return current;
}

static _BTreeMapNode *_BTreeMap_leftmost(const BTreeMap *this)
{
    _BTreeMapNode *current = this->root;

    while (!current->is_leaf)
    {
        current = current->children[0];
    }

    return current;
}

static BTreeMapEntryPos _BTreeMap_next_inorder(const BTreeMap *this, const BTreeMapEntryPos *entry)
{
    _BTreeMapNode *current = entry->node;

    if (current->is_leaf)
    {
        if (entry->kv_idx + 1 < current->key_count)
        {
            return BTreeMapEntryPos_new(current, entry->kv_idx + 1);
        }

        while (current->parent != NULL)
        {
            _BTreeMapNode *parent = current->parent;

            size_t next_idx = _BTreeMapNode_child_pos(parent, current).child_idx;

            if (next_idx < parent->key_count)
            {
                return BTreeMapEntryPos_new(parent, next_idx);
            }

            current = parent;
        }

        BTreeMapEntryPos next = {
            .node = NULL,
        };
        return next;
    }
    else
    {
        return BTreeMapEntryPos_new(_BTreeMap_successor(this, entry), 0);
    }
}

static BTreeMapEntryPos _BTreeMap_previous_inorder(const BTreeMap *this, const BTreeMapEntryPos *entry)
{
    _BTreeMapNode *current = entry->node;

    if (current->is_leaf)
    {
        if (entry->kv_idx > 0)
        {
            return BTreeMapEntryPos_new(current, entry->kv_idx - 1);
        }

        while (current->parent != NULL)
        {
            _BTreeMapNode *parent = current->parent;

            size_t child_idx = _BTreeMapNode_child_pos(parent, current).child_idx;

            if (child_idx > 0)
            {
                return BTreeMapEntryPos_new(parent, child_idx - 1);
            }

            current = parent;
        }

        BTreeMapEntryPos next = {
            .node = NULL,
        };
        return next;
    }
    else
    {
        return _BTreeMap_predecessor(this, entry);
    }
}

typedef enum
{
    GET_RESULT_KIND_FOUND,
    GET_RESULT_KIND_GO_DOWN,
} _GetResultKind;

typedef struct
{
    _GetResultKind kind;

    union
    {
        BTreeMapEntryPos found;
        BTreeMapEntryPos go_down;
    };
} _BTreeMapFindResult;

static _BTreeMapFindResult _BTreeMap_find(BTreeMap *this, const void *key)
{
    _BTreeMapFindResult result;

    _BTreeMapNode *current = this->root;

    while (true)
    {
        BTreeMapEntryPos current_pos = BTreeMapEntryPos_new(current, 0);
        size_t i = 0;

        for (; i < current->key_count && this->key_props.cmp(key, BTreeMapEntryPos_to_entry(current_pos, this).key) > 0;)
        {
            current_pos.kv_idx += 1;
            i += 1;
        }

        if (i < current->key_count && this->key_props.cmp(key, BTreeMapEntryPos_to_entry(current_pos, this).key) == 0)
        {
            result.kind = GET_RESULT_KIND_FOUND;
            result.found.node = current;
            result.found.kv_idx = i;
            break;
        }

        if (current->is_leaf)
        {
            result.kind = GET_RESULT_KIND_GO_DOWN;
            result.go_down.node = current;
            result.go_down.kv_idx = i;
            break;
        }

        current = current->children[i];
    }

    return result;
}

static BTreeMapEntryPos _BTreeMap_lower_bound(const BTreeMap *this, const RangeBound *start)
{
    if (start->kind == RANGE_BOUND_KIND_UNBOUND)
    {
        _BTreeMapNode *leftmost = _BTreeMap_leftmost(this);

        if (leftmost->key_count == 0)
        {
            // if root and empty
            leftmost = NULL;
        }

        return BTreeMapEntryPos_new(leftmost, 0);
    }

    _BTreeMapFindResult result = _BTreeMap_find((BTreeMap *)this, start->kind == RANGE_BOUND_KIND_INCLUDED ? start->included.value : start->excluded.value);

    BTreeMapEntryPos entry;

    if (result.kind == GET_RESULT_KIND_FOUND)
    {
        entry = result.found;

        if (start->kind == RANGE_BOUND_KIND_EXCLUDED)
        {
            entry = _BTreeMap_next_inorder(this, &entry);
        }
    }
    else
    {
        BTreeMapEntryPos entry = result.go_down;

        if (result.go_down.kv_idx >= result.go_down.node->key_count)
        {
            entry = _BTreeMap_next_inorder(this, &entry);
        }
    }

    return entry;
}

static BTreeMapEntryPos _BTreeMap_upper_bound(const BTreeMap *this, const RangeBound *end)
{
    if (end->kind == RANGE_BOUND_KIND_UNBOUND)
    {
        _BTreeMapNode *rightmost = _BTreeMap_rightmost(this);

        if (rightmost->key_count == 0)
        {
            // if root and empty
            rightmost = NULL;
        }

        return BTreeMapEntryPos_new(rightmost, rightmost->key_count - 1);
    }

    _BTreeMapFindResult result = _BTreeMap_find((BTreeMap *)this, end->kind == RANGE_BOUND_KIND_INCLUDED ? end->included.value : end->excluded.value);

    BTreeMapEntryPos entry;

    if (result.kind == GET_RESULT_KIND_FOUND)
    {
        entry = result.found;

        if (end->kind == RANGE_BOUND_KIND_EXCLUDED)
        {
            entry = _BTreeMap_previous_inorder(this, &entry);
        }
    }
    else
    {
        entry = result.go_down;

        if (result.go_down.kv_idx == 0)
        {
            entry = _BTreeMap_previous_inorder(this, &entry);
        }
    }

    return entry;
}

const void *BTreeMap_get(BTreeMap *this, const void *key)
{
    _BTreeMapFindResult result = _BTreeMap_find(this, key);

    if (result.kind != GET_RESULT_KIND_FOUND)
    {
        return NULL;
    }

    return BTreeMapEntryPos_to_entry(result.found, this).value;
}

static void _BTreeMap_fix_overflow_up(BTreeMap *this, _BTreeMapNode *node)
{
    _BTreeMapNode *current = node;

    while (current->key_count == BTREEMAP_MAXIMUM_KEY_COUNT)
    {
        BTreeMapEntryPos separator_pos;
        _BTreeMapNode *right = malloc(_BTreeMap_node_size(this));
        _BTreeMap_split(this, current, right, &separator_pos);

        BTreeMapEntry separator = BTreeMapEntryPos_to_entry(separator_pos, this);

        if (current->parent == NULL)
        {
            _BTreeMapNode *new_root = malloc(_BTreeMap_node_size(this));
            _BTreeMapNode_new(new_root, NULL, false);
            this->root = new_root;

            BTreeMapEntryPos entry_pos = {
                .node = new_root,
                .kv_idx = 0,
            };
            _BTreeMap_insert_entry_at(this, &separator, &entry_pos);

            new_root->children[0] = current;
            current->parent = new_root;

            new_root->children[1] = right;
            right->parent = new_root;

            _BTreeMap_remove_entry_at(this, &separator_pos);
            break;
        }
        else
        {
            _BTreeMapNode *parent = current->parent;
            BTreeMapChildPos child_pos = _BTreeMapNode_child_pos(parent, current);

            BTreeMapEntryPos entry_pos = {
                .node = parent,
                .kv_idx = child_pos.child_idx,
            };
            _BTreeMap_insert_entry_at(this, &separator, &entry_pos);
            parent->children[child_pos.child_idx + 1] = right;

            _BTreeMap_remove_entry_at(this, &separator_pos);

            current = parent;
        }
    }
}

size_t BTreeMap_len(const BTreeMap *this)
{
    return this->length;
}

void BTreeMap_insert(BTreeMap *this, const void *key, const void *value)
{
    BTreeMapEntry new_entry = _BTreeMapEntry_new((void *)key, (void *)value);

    _BTreeMapFindResult result = _BTreeMap_find(this, key);

    if (result.kind == GET_RESULT_KIND_FOUND)
    {
        _BTreeMap_drop_entry_at(this, &result.found);
        _BTreeMap_replace_entry_at(this, &new_entry, &result.found);
    }
    else
    {
        _BTreeMap_insert_entry_at(this, &new_entry, &result.go_down);

        this->length += 1;

        _BTreeMap_fix_overflow_up(this, result.go_down.node);
    }
}

static void _BTreeMap_borrow(const BTreeMap *this, BTreeMapEntryPos from_pos, BTreeMapChildPos from_child_pos, BTreeMapEntryPos separator_pos, BTreeMapEntryPos to_pos, BTreeMapChildPos to_child_pos)
{
    BTreeMapEntry separator = BTreeMapEntryPos_to_entry(separator_pos, this);
    _BTreeMap_insert_entry_at(this, &separator, &to_pos);

    BTreeMapEntry from = BTreeMapEntryPos_to_entry(from_pos, this);
    _BTreeMap_replace_entry_at(this, &from, &separator_pos);

    _BTreeMap_remove_entry_at(this, &from_pos);

    if (!from_pos.node->is_leaf)
    {
        _BTreeMapNode *from_child = BTreeMapChildPos_to_child(from_child_pos);
        _BTreeMap_insert_child_at(this, from_child, to_child_pos);
        _BTreeMap_remove_child_at(from_child_pos);
    }
}

static void _BTreeMap_merge(const BTreeMap *this, _BTreeMapNode *left, _BTreeMapNode *right, BTreeMapEntryPos separator_pos)
{
    BTreeMapEntryPos left_last_entry = BTreeMapEntryPos_new(left, left->key_count);

    BTreeMapEntry separator = BTreeMapEntryPos_to_entry(separator_pos, this);
    _BTreeMap_insert_entry_at(this, &separator, &left_last_entry);
    left_last_entry.kv_idx += 1;

    BTreeMapEntryPos right_first_entry = BTreeMapEntryPos_new(right, 0);
    _BTreeMap_move_entries(this, right_first_entry, left_last_entry);
    left->key_count++;

    if (!left->is_leaf)
    {
        _BTreeMap_move_children(BTreeMapChildPos_new(right, 0), BTreeMapChildPos_new(left, left->key_count + 1));
    }

    free(right);

    _BTreeMap_remove_entry_at(this, &separator_pos);
    _BTreeMap_remove_child_at(BTreeMapChildPos_new(separator_pos.node, separator_pos.kv_idx + 1));
}

static void _BTreeMap_fix_underflow_up(BTreeMap *this, _BTreeMapNode *node)
{
    _BTreeMapNode *current = node;

    while (current->key_count < BTREEMAP_MINIMUM_KEY_COUNT)
    {
        bool is_root = current->parent == NULL;
        if (is_root)
        {
            if (!current->is_leaf && current->key_count == 0)
            {
                _BTreeMapNode *old_root = this->root;

                this->root = this->root->children[0];
                this->root->parent = NULL;

                free(old_root);
            }

            break;
        }

        _BTreeMapNode *parent = current->parent;
        size_t child_idx = _BTreeMapNode_child_pos(parent, current).child_idx;

        bool has_left_sibling = child_idx > 0;
        bool has_right_sibling = child_idx < parent->key_count + 1;

        if (has_left_sibling && parent->children[child_idx - 1]->key_count > BTREEMAP_MINIMUM_KEY_COUNT)
        {
            _BTreeMapNode *left = parent->children[child_idx - 1];
            size_t separator_idx = child_idx - 1;

            _BTreeMap_borrow(
                this,
                BTreeMapEntryPos_new(left, left->key_count - 1),
                BTreeMapChildPos_new(left, left->key_count),
                BTreeMapEntryPos_new(parent, separator_idx),
                BTreeMapEntryPos_new(current, 0),
                BTreeMapChildPos_new(current, 0));

            break;
        }
        else if (has_right_sibling && parent->children[child_idx + 1]->key_count > BTREEMAP_MINIMUM_KEY_COUNT)
        {
            _BTreeMapNode *right = parent->children[child_idx + 1];
            size_t separator_idx = child_idx;

            _BTreeMap_borrow(
                this,
                BTreeMapEntryPos_new(right, 0),
                BTreeMapChildPos_new(right, 0),
                BTreeMapEntryPos_new(parent, separator_idx),
                BTreeMapEntryPos_new(current, current->key_count),
                BTreeMapChildPos_new(current, current->key_count + 1));

            break;
        }
        else
        {
            size_t left_idx = has_right_sibling ? child_idx : child_idx - 1;

            _BTreeMap_merge(
                this,
                parent->children[left_idx],
                parent->children[left_idx + 1],
                BTreeMapEntryPos_new(parent, left_idx));

            current = parent;
        }
    }
}

void BTreeMap_remove(BTreeMap *this, const void *key)
{
    _BTreeMapFindResult result = _BTreeMap_find(this, key);

    if (result.kind != GET_RESULT_KIND_FOUND)
    {
        return;
    }

    BTreeMapEntryPos entry_pos = result.found;
    BTreeMapEntry entry = BTreeMapEntryPos_to_entry(entry_pos, this);

    if (!entry_pos.node->is_leaf)
    {
        BTreeMapEntryPos pred_pos = _BTreeMap_predecessor(this, &result.found);
        BTreeMapEntry pred = BTreeMapEntryPos_to_entry(pred_pos, this);

        _swap(entry.key, pred.key, this->key_props.size);
        _swap(entry.value, pred.value, this->value_props.size);

        entry_pos = pred_pos;
    }

    _BTreeMap_drop_entry_at(this, &entry_pos);
    _BTreeMap_remove_entry_at(this, &entry_pos);

    this->length -= 1;

    _BTreeMap_fix_underflow_up(this, entry_pos.node);
}

void BTreeMap_new(BTreeMap *this, const BTreeMapKeyProps *key_props, const BTreeMapValueProps *value_props)
{
    this->key_props = *key_props;
    this->value_props = *value_props;

    this->length = 0;
    this->root = malloc(_BTreeMap_node_size(this));
    _BTreeMapNode_new(this->root, NULL, true);
}

void BTreeMap_drop(BTreeMap *this)
{
    if (this->length > 0)
    {
        BTreeMapEntryPos current = BTreeMapEntryPos_new(_BTreeMap_leftmost(this), 0);

        while (current.node != NULL)
        {
            _BTreeMap_drop_entry_at(this, &current);
            current = _BTreeMap_next_inorder(this, &current);
        }
    }

    free(this->root);
}

// [BTreeMapRangeIter]

typedef struct
{
    const BTreeMap *map;
    BTreeMapEntryPos current;
    BTreeMapEntryPos current_back;
    BTreeMapEntry buffer;
    bool is_done;
} BTreeMapRangeIter;

void BTreeMapRangeIter_new(BTreeMapRangeIter *this, const BTreeMap *map, const RangeBound *start, const RangeBound *end)
{
    this->map = map;

    BTreeMapEntryPos lower_bound = _BTreeMap_lower_bound(this->map, start);
    BTreeMapEntryPos upper_bound = _BTreeMap_upper_bound(this->map, end);

    if (lower_bound.node == NULL || upper_bound.node == NULL)
    {
        this->is_done = true;
    }
    else
    {
        this->is_done = false;
        this->current = lower_bound;
        this->current_back = upper_bound;
    }
}

BTreeMapEntry *BTreeMapRangeIter_next(BTreeMapRangeIter *this)
{
    if (this->is_done)
    {
        return NULL;
    }

    bool is_last = this->current.node == this->current_back.node && this->current.kv_idx == this->current_back.kv_idx;

    if (is_last)
    {
        this->is_done = true;
        this->buffer = BTreeMapEntryPos_to_entry(this->current, this->map);
        return &this->buffer;
    }
    else
    {
        this->buffer = BTreeMapEntryPos_to_entry(this->current, this->map);
        this->current = _BTreeMap_next_inorder(this->map, &this->current);
        return &this->buffer;
    }
}

void *BTreeMapRangeIter_next_back(BTreeMapRangeIter *this)
{
    if (this->is_done)
    {
        return NULL;
    }

    bool is_last = this->current.node == this->current_back.node && this->current.kv_idx == this->current_back.kv_idx;

    if (is_last)
    {
        this->is_done = true;
        this->buffer = BTreeMapEntryPos_to_entry(this->current_back, this->map);
        return &this->buffer;
    }
    else
    {
        this->buffer = BTreeMapEntryPos_to_entry(this->current_back, this->map);
        this->current_back = _BTreeMap_previous_inorder(this->map, &this->current_back);
        return &this->buffer;
    }
}

void BTreeMapRangeIter_drop(BTreeMapRangeIter *this)
{
}

BTreeMapRangeIter BTreeMap_range(const BTreeMap *this, const RangeBound *start, const RangeBound *end)
{
    BTreeMapRangeIter iter;
    BTreeMapRangeIter_new(&iter, this, start, end);

    return iter;
}

// [BTreeSet]

typedef struct
{
    size_t size;
    CmpFn cmp;
    DropFn drop;
} BTreeSetElementProps;

typedef struct
{
    BTreeMap *map;
} BTreeSet;

void BTreeSet_new(BTreeSet *this, const BTreeSetElementProps *element_props)
{
    this->map = malloc(sizeof(*this->map));

    BTreeMapKeyProps key_props = {
        .size = element_props->size,
        .cmp = element_props->cmp,
        .drop = element_props->drop,
    };
    BTreeMapValueProps value_props = {
        .size = sizeof(uint8_t),
    };
    BTreeMap_new(this->map, &key_props, &value_props);
}

bool BTreeSet_contains(const BTreeSet *this, const void *element)
{
    return BTreeMap_get(this->map, element) == NULL ? false : true;
}

size_t BTreeSet_len(const BTreeSet *this)
{
    return BTreeMap_len(this->map);
}

void BTreeSet_drop(BTreeSet *this)
{
    BTreeMap_drop(this->map);
    free(this->map);
}

void BTreeSet_insert(BTreeSet *this, const void *element)
{
    uint8_t i = 0;
    BTreeMap_insert(this->map, element, &i);
}

void BTreeSet_remove(BTreeSet *this, const void *element)
{
    BTreeMap_remove(this->map, element);
}

// [BTreeSetRangeIter]

typedef struct
{
    BTreeMapRangeIter *iter;
} BTreeSetRangeIter;

void BTreeSetRangeIter_new(BTreeSetRangeIter *this, const BTreeSet *set, const RangeBound *start, const RangeBound *end)
{
    this->iter = malloc(sizeof(*this->iter));
    BTreeMapRangeIter_new(this->iter, set->map, start, end);
}

void *BTreeSetRangeIter_next(BTreeSetRangeIter *this)
{
    BTreeMapEntry *next = BTreeMapRangeIter_next(this->iter);

    return next == NULL ? NULL : next->key;
}

void *BTreeSetRangeIter_next_back(BTreeSetRangeIter *this)
{
    BTreeMapEntry *next = BTreeMapRangeIter_next_back(this->iter);

    return next == NULL ? NULL : next->key;
}

void BTreeSetRangeIter_drop(BTreeSetRangeIter *this)
{
    BTreeMapRangeIter_drop(this->iter);
}

BTreeSetRangeIter BTreeSet_range(const BTreeSet *this, const RangeBound *start, const RangeBound *end)
{
    BTreeSetRangeIter iter;
    BTreeSetRangeIter_new(&iter, this, start, end);

    return iter;
}

// [BTreeSetUnionIter]

typedef enum
{
    BTREE_SET_UNION_ITER_SIDE_A,
    BTREE_SET_UNION_ITER_SIDE_B,
} BTreeSetUnionIterSide;

typedef struct
{
    const BTreeSet *a;
    const BTreeSet *b;
    BTreeMapEntryPos current_a;
    bool a_is_done;
    BTreeMapEntryPos current_b;
    bool b_is_done;
} BTreeSetUnionIter;

void BTreeSetUnionIter_new(BTreeSetUnionIter *this, const BTreeSet *a, const BTreeSet *b)
{
    this->a = a;
    this->b = b;

    if (BTreeSet_len(this->a) == 0)
    {
        this->a_is_done = true;
    }
    else
    {
        this->current_a = BTreeMapEntryPos_new(_BTreeMap_leftmost(this->a->map), 0);
        this->a_is_done = false;
    }

    if (BTreeSet_len(this->b) == 0)
    {
        this->a_is_done = true;
    }
    else
    {
        this->current_b = BTreeMapEntryPos_new(_BTreeMap_leftmost(this->b->map), 0);
        this->b_is_done = false;
    }
}

void *BTreeSetUnionIter_next(BTreeSetUnionIter *this)
{
    if (this->a_is_done && this->b_is_done)
    {
        return NULL;
    }

    BTreeSetUnionIterSide should_advance;

    if (this->a_is_done)
    {
        should_advance = BTREE_SET_UNION_ITER_SIDE_B;
    }
    else if (this->b_is_done)
    {
        should_advance = BTREE_SET_UNION_ITER_SIDE_A;
    }
    else
    {
        BTreeMapEntry current_a = BTreeMapEntryPos_to_entry(this->current_a, this->a->map);
        BTreeMapEntry current_b = BTreeMapEntryPos_to_entry(this->current_b, this->b->map);

        int_fast8_t result = this->a->map->key_props.cmp(current_a.key, current_b.key);

        if (result == 0)
        {
            this->current_b = _BTreeMap_next_inorder(this->b->map, &this->current_b);
            this->b_is_done = this->current_b.node == NULL ? true : false;

            should_advance = BTREE_SET_UNION_ITER_SIDE_A;
        }
        else
        {
            should_advance = result > 0 ? BTREE_SET_UNION_ITER_SIDE_B : BTREE_SET_UNION_ITER_SIDE_A;
        }
    }

    BTreeMapEntry current;

    if (should_advance == BTREE_SET_UNION_ITER_SIDE_A)
    {
        current = BTreeMapEntryPos_to_entry(this->current_a, this->a->map);
        this->current_a = _BTreeMap_next_inorder(this->a->map, &this->current_a);
        this->a_is_done = this->current_a.node == NULL ? true : false;
    }
    else
    {
        current = BTreeMapEntryPos_to_entry(this->current_b, this->b->map);
        this->current_b = _BTreeMap_next_inorder(this->b->map, &this->current_b);
        this->b_is_done = this->current_b.node == NULL ? true : false;
    }

    return current.key;
}

void BTreeSetUnionIter_drop(BTreeSetUnionIter *this)
{
}

BTreeSetUnionIter BTreeSet_union(const BTreeSet *this, const BTreeSet *other)
{
    BTreeSetUnionIter iter;
    BTreeSetUnionIter_new(&iter, this, other);

    return iter;
}

// [BTreeSetIntersectionIter]

typedef enum
{
    BTREE_SET_INTERSECTION_ITER_SIDE_A,
    BTREE_SET_INTERSECTION_ITER_SIDE_B,
} BTreeSetIntersectionIterSide;

typedef struct
{
    const BTreeSet *a;
    const BTreeSet *b;
    BTreeMapEntryPos current;
    bool is_done;
} BTreeSetIntersectionIter;

void BTreeSetIntersectionIter_new(BTreeSetIntersectionIter *this, const BTreeSet *a, const BTreeSet *b)
{
    this->a = a;
    this->b = b;

    if (BTreeSet_len(this->a) == 0 || BTreeSet_len(this->b) == 0)
    {
        this->is_done = true;
    }
    else
    {
        this->is_done = false;
        this->current = BTreeMapEntryPos_new(_BTreeMap_leftmost(this->a->map), 0);
    }
}

void *BTreeSetIntersectionIter_next(BTreeSetIntersectionIter *this)
{
    if (this->is_done)
    {
        return NULL;
    }

    BTreeSetIntersectionIterSide current_side = BTREE_SET_INTERSECTION_ITER_SIDE_A;
    BTreeMapEntryPos current = this->current;
    const BTreeSet *current_set = this->a;
    const BTreeSet *other_set = this->b;

    while (current.node != NULL)
    {
        BTreeMapEntry current_entry = BTreeMapEntryPos_to_entry(current, current_set->map);
        _BTreeMapFindResult result = _BTreeMap_find((BTreeMap *)other_set->map, current_entry.key);

        if (result.kind == GET_RESULT_KIND_FOUND)
        {
            current = current_side == BTREE_SET_INTERSECTION_ITER_SIDE_A ? current : result.found;
            break;
        }
        else
        {
            if (result.go_down.kv_idx < result.go_down.node->key_count)
            {
                current = result.go_down;
            }
            else
            {
                current = _BTreeMap_next_inorder(this->a->map, &this->current);
            }

            if (current_side == BTREE_SET_INTERSECTION_ITER_SIDE_A)
            {
                current_side = BTREE_SET_INTERSECTION_ITER_SIDE_B;
                current_set = this->b;
                other_set = this->a;
            }
            else
            {
                current_side = BTREE_SET_INTERSECTION_ITER_SIDE_A;
                current_set = this->a;
                other_set = this->b;
            }
        }
    }

    if (current.node == NULL)
    {
        this->is_done = true;
        return NULL;
    }
    else
    {
        this->current = _BTreeMap_next_inorder(this->a->map, &current);
        BTreeMapEntry current_entry = BTreeMapEntryPos_to_entry(current, this->a->map);
        return current_entry.key;
    }
}

void BTreeSetIntersectionIter_drop(BTreeSetIntersectionIter *this)
{
}

BTreeSetIntersectionIter BTreeSet_intersection(const BTreeSet *this, const BTreeSet *other)
{
    BTreeSetIntersectionIter iter;
    BTreeSetIntersectionIter_new(&iter, this, other);

    return iter;
}

// [BTreeSetDifferenceIter]

typedef struct
{
    const BTreeSet *a;
    const BTreeSet *b;
    BTreeMapEntryPos current;
    bool is_done;
} BTreeSetDifferenceIter;

void BTreeSetDifferenceIter_new(BTreeSetDifferenceIter *this, const BTreeSet *a, const BTreeSet *b)
{
    this->a = a;
    this->b = b;

    if (BTreeSet_len(this->a) == 0)
    {
        this->is_done = true;
    }
    else
    {
        this->current = BTreeMapEntryPos_new(_BTreeMap_leftmost(this->a->map), 0);
        this->is_done = false;
    }
}

void *BTreeSetDifferenceIter_next(BTreeSetDifferenceIter *this)
{
    if (this->is_done)
    {
        return NULL;
    }

    BTreeMapEntryPos current = this->current;
    BTreeMapEntry current_entry = {};

    while (current.node != NULL)
    {
        current_entry = BTreeMapEntryPos_to_entry(current, this->a->map);
        _BTreeMapFindResult result = _BTreeMap_find(this->b->map, current_entry.key);

        if (result.kind != GET_RESULT_KIND_FOUND)
        {
            break;
        }

        current = _BTreeMap_next_inorder(this->a->map, &current);
    }

    if (current.node == NULL)
    {
        this->is_done = true;
        return NULL;
    }
    else
    {
        this->current = _BTreeMap_next_inorder(this->a->map, &current);
        return current_entry.key;
    }
}

void BTreeSetDifferenceIter_drop(BTreeSetDifferenceIter *this)
{
}

BTreeSetDifferenceIter BTreeSet_difference(const BTreeSet *this, const BTreeSet *other)
{
    BTreeSetDifferenceIter iter;
    BTreeSetDifferenceIter_new(&iter, this, other);

    return iter;
}

// [BTreeSetSymmetricDifferenceIter]

typedef enum
{
    BTREE_SET_SYMMETRIC_DIFFERENCE_ITER_SIDE_A,
    BTREE_SET_SYMMETRIC_DIFFERENCE_ITER_SIDE_B,
} BTreeSetSymmetricDifferenceIterSide;

typedef struct
{
    const BTreeSet *a;
    const BTreeSet *b;
    BTreeMapEntryPos current_a;
    bool a_is_done;
    BTreeMapEntryPos current_b;
    bool b_is_done;
} BTreeSetSymmetricDifferenceIter;

void BTreeSetSymmetricDifferenceIter_new(BTreeSetSymmetricDifferenceIter *this, const BTreeSet *a, const BTreeSet *b)
{
    this->a = a;
    this->b = b;

    if (BTreeSet_len(this->a) == 0)
    {
        this->a_is_done = true;
    }
    else
    {
        this->current_a = BTreeMapEntryPos_new(_BTreeMap_leftmost(this->a->map), 0);
        this->a_is_done = false;
    }

    if (BTreeSet_len(this->b) == 0)
    {
        this->a_is_done = true;
    }
    else
    {
        this->current_b = BTreeMapEntryPos_new(_BTreeMap_leftmost(this->b->map), 0);
        this->b_is_done = false;
    }
}

void *BTreeSetSymmetricDifferenceIter_next(BTreeSetSymmetricDifferenceIter *this)
{
    if (this->a_is_done && this->b_is_done)
    {
        return NULL;
    }

    BTreeSetSymmetricDifferenceIterSide should_advance;

    while (true)
    {
        if (this->a_is_done)
        {
            should_advance = BTREE_SET_UNION_ITER_SIDE_B;
        }
        else if (this->b_is_done)
        {
            should_advance = BTREE_SET_UNION_ITER_SIDE_A;
        }
        else
        {
            BTreeMapEntry current_a = BTreeMapEntryPos_to_entry(this->current_a, this->a->map);
            BTreeMapEntry current_b = BTreeMapEntryPos_to_entry(this->current_b, this->b->map);

            int_fast8_t result = this->a->map->key_props.cmp(current_a.key, current_b.key);

            if (result == 0)
            {
                this->current_a = _BTreeMap_next_inorder(this->a->map, &this->current_a);
                this->a_is_done = this->current_a.node == NULL;

                this->current_b = _BTreeMap_next_inorder(this->b->map, &this->current_b);
                this->b_is_done = this->current_b.node == NULL;

                continue;
            }
            else
            {
                should_advance = result > 0 ? BTREE_SET_UNION_ITER_SIDE_B : BTREE_SET_UNION_ITER_SIDE_A;
            }
        }

        break;
    }

    BTreeMapEntry current;

    if (should_advance == BTREE_SET_SYMMETRIC_DIFFERENCE_ITER_SIDE_A)
    {
        current = BTreeMapEntryPos_to_entry(this->current_a, this->a->map);
        this->current_a = _BTreeMap_next_inorder(this->a->map, &this->current_a);
        this->a_is_done = this->current_a.node == NULL ? true : false;
    }
    else
    {
        current = BTreeMapEntryPos_to_entry(this->current_b, this->b->map);
        this->current_b = _BTreeMap_next_inorder(this->b->map, &this->current_b);
        this->b_is_done = this->current_b.node == NULL ? true : false;
    }

    return current.key;
}

void BTreeSetSymmetricDifferenceIter_drop(BTreeSetSymmetricDifferenceIter *this)
{
}

BTreeSetSymmetricDifferenceIter BTreeSet_symmetric_difference(const BTreeSet *this, const BTreeSet *other)
{
    BTreeSetSymmetricDifferenceIter iter;
    BTreeSetSymmetricDifferenceIter_new(&iter, this, other);

    return iter;
}

// [LinkedList]

#include <stddef.h>

typedef struct _LinkedListNode
{
    struct _LinkedListNode *next;
    struct _LinkedListNode *previous;
} LinkedListNode;

typedef struct
{
    size_t element_size;
    DropFn drop;
} LinkedListElementProps;

typedef struct
{
    size_t length;
    LinkedListNode *head;
    LinkedListNode *tail;
    LinkedListElementProps element_props;
} LinkedList;

static void *_LinkedListNode_get_data(LinkedListNode *this)
{
    return (void *)((uintptr_t)(this) + ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(*this)));
}

static void _LinkedList_drop_element(LinkedList *this, void *element)
{
    if (this->element_props.drop)
    {
        this->element_props.drop(element);
    }
}

void LinkedList_new(LinkedList *this, const LinkedListElementProps *element_props)
{
    this->element_props = *element_props;

    this->length = 0;
    this->head = NULL;
    this->tail = NULL;
}

size_t LinkedList_len(const LinkedList *this)
{
    return this->length;
}

static void _LinkedList_insert_after(LinkedList *this, LinkedListNode *position, LinkedListNode *new_node)
{
    bool insert_at_head = position == NULL;
    if (insert_at_head)
    {
        new_node->previous = NULL;

        bool list_is_empty = this->head == NULL;
        if (list_is_empty)
        {
            new_node->next = NULL;

            this->head = new_node;
            this->tail = new_node;
        }
        else
        {
            new_node->next = this->head;
            this->head->previous = new_node;
            this->head = new_node;
        }
    }
    else
    {
        new_node->next = position->next;
        new_node->previous = position;

        bool insert_after_tail = position->next == NULL;
        if (insert_after_tail)
        {
            this->tail = new_node;
        }
        else
        {
            position->next->previous = new_node;
        }

        position->next = new_node;
    }

    this->length++;
}

static void _LinkedList_remove(LinkedList *this, LinkedListNode *position)
{
    if (position->previous)
    {
        position->previous->next = position->next;
    }
    else
    {
        this->head = position->next;
    }

    if (position->next)
    {
        position->next->previous = position->previous;
    }
    else
    {
        this->tail = position->previous;
    }

    _LinkedList_drop_element(this, _LinkedListNode_get_data(position));
    free(position);

    this->length--;
}

void LinkedList_push_front(LinkedList *this, const void *value)
{
    LinkedListNode *new_node = malloc(ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(LinkedListNode)) + this->element_props.element_size);

    memcpy(_LinkedListNode_get_data(new_node), value, this->element_props.element_size);

    _LinkedList_insert_after(this, NULL, new_node);
}

void LinkedList_push_back(LinkedList *this, const void *value)
{
    LinkedListNode *new_node = malloc(ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(LinkedListNode)) + this->element_props.element_size);

    memcpy(_LinkedListNode_get_data(new_node), value, this->element_props.element_size);

    _LinkedList_insert_after(this, this->tail, new_node);
}

void LinkedList_pop_front(LinkedList *this)
{
    if (this->head != NULL)
    {
        _LinkedList_remove(this, this->head);
    }
}

void LinkedList_pop_back(LinkedList *this)
{
    if (this->tail != NULL)
    {
        _LinkedList_remove(this, this->tail);
    }
}

void *LinkedList_front(LinkedList *this)
{
    if (this->head == NULL)
    {
        return NULL;
    }
    else
    {
        return _LinkedListNode_get_data(this->head);
    }
}

void *LinkedList_back(LinkedList *this)
{
    if (this->tail == NULL)
    {
        return NULL;
    }
    else
    {
        return _LinkedListNode_get_data(this->tail);
    }
}

void LinkedList_drop(LinkedList *this)
{
    for (LinkedListNode *current = this->head; current != NULL;)
    {
        _LinkedList_drop_element(this, _LinkedListNode_get_data(current));

        LinkedListNode *next = current->next;
        free(current);
        current = next;
    }
}

// [VecDeque]

typedef struct
{
    size_t size;
    DropFn drop;
} VecDequeElementProps;

typedef struct
{
    size_t length;
    size_t capacity;
    void *data;
    size_t head;
    VecDequeElementProps element_props;
} VecDeque;

void VecDeque_new(VecDeque *this, const VecDequeElementProps *element_props)
{
    this->element_props = *element_props;

    this->length = 0;
    this->capacity = 0;
    this->data = NULL;
}

size_t VecDeque_len(const VecDeque *this)
{
    return this->length;
}

size_t VecDeque_capacity(const VecDeque *this)
{
    return this->capacity;
}

static uint8_t *_VecDeque_get(const VecDeque *this, size_t index)
{
    return (uint8_t *)(this->data) + (index * this->element_props.size);
}

void *VecDeque_front(VecDeque *this)
{
    if (this->length > 0)
    {
        return _VecDeque_get(this, this->head);
    }
    else
    {
        return NULL;
    }
}

void *VecDeque_back(VecDeque *this)
{
    if (this->length > 0)
    {
        size_t tail_index = (this->head + this->length - 1) % this->capacity;
        return _VecDeque_get(this, tail_index);
    }
    else
    {
        return NULL;
    }
}

static void _VecDeque_grow_buffer(VecDeque *this)
{
    assert(this->length == this->capacity);

    size_t new_capacity = this->capacity ? this->capacity * 2 : 10;
    void *new_data = malloc(new_capacity * this->element_props.size);

    if (this->capacity)
    {
        size_t right = this->capacity - this->head;
        memcpy(new_data, VecDeque_front(this), right * this->element_props.size);

        size_t left = this->capacity - right;
        if (left > 0)
        {
            memcpy(new_data + (right * this->element_props.size), this->data, left * this->element_props.size);
        }

        free(this->data);
    }

    this->data = new_data;
    this->capacity = new_capacity;
    this->head = 0;
}

void VecDeque_push_back(VecDeque *this, const void *value)
{
    if (this->length == this->capacity)
    {
        _VecDeque_grow_buffer(this);
    }

    size_t insert_index = (this->head + this->length) % this->capacity;
    memcpy(_VecDeque_get(this, insert_index), value, this->element_props.size);

    this->length++;
}

void VecDeque_push_front(VecDeque *this, const void *value)
{
    if (this->length == this->capacity)
    {
        _VecDeque_grow_buffer(this);
    }

    this->head = (this->head + this->capacity - 1) % this->capacity;
    memcpy(_VecDeque_get(this, this->head), value, this->element_props.size);

    this->length++;
}

void VecDeque_pop_back(VecDeque *this)
{
    if (this->length > 0)
    {
        if (this->element_props.drop != NULL)
        {
            this->element_props.drop(VecDeque_back(this));
        }

        this->length--;
    }
}

void VecDeque_pop_front(VecDeque *this)
{
    if (this->length > 0)
    {
        this->element_props.drop(VecDeque_front(this));
        this->head = (this->head + 1) % this->capacity;
        this->length--;
    }
}

void VecDeque_clear(VecDeque *this)
{
    while (this->length > 0)
    {
        VecDeque_pop_back(this);
    }
}

void VecDeque_shrink_to_fit(VecDeque *this)
{
    if (this->length == this->capacity)
    {
        return;
    }

    if (this->length == 0)
    {
        free(this->data);
        this->data = NULL;
        this->capacity = 0;
    }
    else
    {
        void *new_data = malloc(this->length * this->element_props.size);

        size_t right = MIN(this->length, this->capacity - this->head);
        memcpy(new_data, VecDeque_front(this), right * this->element_props.size);

        size_t left = this->length - right;
        if (left > 0)
        {
            memcpy((uint8_t *)(new_data) + (right * this->element_props.size), this->data, left * this->element_props.size);
        }

        free(this->data);

        this->data = new_data;
        this->capacity = this->length;
    }

    this->head = 0;
}

void VecDeque_drop(VecDeque *this)
{
    VecDeque_clear(this);
    free(this->data);
}

// [BinaryHeap]

#define PARENT(i) ((i - 1) / 2)
#define LEFT(i) ((2 * i) + 1)
#define RIGHT(i) (LEFT(i) + 1)

typedef struct
{
    size_t size;
    DropFn drop;
    CmpFn cmp;
} BinaryHeapElementProps;

typedef struct
{
    Vec *buffer;
    BinaryHeapElementProps element_props;
} BinaryHeap;

void BinaryHeap_new(BinaryHeap *this, const BinaryHeapElementProps *element_props)
{
    this->element_props = *element_props;

    this->buffer = malloc(sizeof(*this->buffer));

    VecElementOps ops;
    ops.drop = this->element_props.drop;
    Vec_new(this->buffer, this->element_props.size, &ops);
}

static void _BinaryHeap_swap(const BinaryHeap *this, size_t idx_a, size_t idx_b)
{
    uint8_t tmp[this->element_props.size];

    memcpy(tmp, Vec_get(this->buffer, idx_a), this->element_props.size);
    memcpy(Vec_get_mut(this->buffer, idx_a), Vec_get(this->buffer, idx_b), this->element_props.size);
    memcpy(Vec_get_mut(this->buffer, idx_b), tmp, this->element_props.size);
}

static void _BinaryHeap_sift_up(BinaryHeap *this, size_t i)
{
    while (i > 0)
    {
        size_t parent = PARENT(i);

        if (this->element_props.cmp(Vec_get(this->buffer, i), Vec_get(this->buffer, parent)) <= 0)
        {
            break;
        }

        _BinaryHeap_swap(this, i, parent);
        i = parent;
    }
}

static void _BinaryHeap_sift_down(BinaryHeap *this, size_t i)
{
    size_t length = Vec_len(this->buffer);

    while (true)
    {
        size_t left = LEFT(i);
        size_t right = RIGHT(i);

        size_t largest = i;

        if (left < length && this->element_props.cmp(Vec_get(this->buffer, left), Vec_get(this->buffer, largest)) > 0)
        {
            largest = left;
        }

        if (right < length && this->element_props.cmp(Vec_get(this->buffer, right), Vec_get(this->buffer, largest)) > 0)
        {
            largest = right;
        }

        if (largest == i)
        {
            break;
        }

        _BinaryHeap_swap(this, i, largest);
        i = largest;
    }
}

const void *BinaryHeap_peek(const BinaryHeap *this)
{
    if (Vec_len(this->buffer) == 0)
    {
        return NULL;
    }
    else
    {
        return Vec_get(this->buffer, 0);
    }
}

void *BinaryHeap_peek_mut(BinaryHeap *this)
{
    return (void *)BinaryHeap_peek(this);
}

void BinaryHeap_push(BinaryHeap *this, const void *value)
{
    Vec_push(this->buffer, value);

    _BinaryHeap_sift_up(this, Vec_len(this->buffer) - 1);
}

void BinaryHeap_pop(BinaryHeap *this)
{
    if (Vec_len(this->buffer) == 0)
    {
        return;
    }

    _BinaryHeap_swap(this, 0, Vec_len(this->buffer) - 1);
    Vec_pop(this->buffer);

    if (Vec_len(this->buffer) > 0)
    {
        _BinaryHeap_sift_down(this, 0);
    }
}

size_t BinaryHeap_len(const BinaryHeap *this)
{
    return Vec_len(this->buffer);
}

void BinaryHeap_drop(BinaryHeap *this)
{
    Vec_drop(this->buffer);
    free(this->buffer);
}

// [Hasher]

typedef void (*HasherResetFn)(void *this);
typedef void (*HasherWriteFn)(void *this, const void *data, size_t length);
typedef uint64_t (*HasherFinishFn)(const void *this);

typedef struct
{
    size_t size;
    HasherResetFn reset;
    HasherWriteFn write;
    HasherFinishFn finish;
    DropFn drop;
} HasherProps;

typedef struct
{
    void *concrete_hasher;
    HasherProps props;
} Hasher;

void Hasher_new(Hasher *this, const HasherProps *props, const void *concrete_hasher)
{
    this->props = *props;

    this->concrete_hasher = malloc(this->props.size);
    memcpy(this->concrete_hasher, concrete_hasher, this->props.size);
}

void Hasher_reset(Hasher *this)
{
    this->props.reset(this->concrete_hasher);
}

void Hasher_write(Hasher *this, const void *data, size_t length)
{
    this->props.write(this->concrete_hasher, data, length);
}

uint64_t Hasher_finish(const Hasher *this)
{
    return this->props.finish(this->concrete_hasher);
}

void Hasher_drop(Hasher *this)
{
    if (this->props.drop != NULL)
    {
        this->props.drop(this->concrete_hasher);
    }

    free(this->concrete_hasher);
}

// [SimpleHasher]

typedef struct
{
    uint64_t state;
} SimpleHasher;

void SimpleHasher_reset(SimpleHasher *this)
{
    this->state = 0;
}

void SimpleHasher_write(SimpleHasher *this, const void *data, size_t length)
{
    const uint8_t *bytes = data;

    for (size_t i = 0; i < length; i++)
    {
        this->state += bytes[i];
    }
}

uint64_t SimpleHasher_finish(const SimpleHasher *this)
{
    return this->state;
}

// [HashMap]

typedef void (*HashFn)(const void *key, Hasher *hasher);

typedef struct
{
    bool is_empty;
    size_t probe_length;
} _HashMapEntry;

typedef struct
{
    size_t size;
    EqFn eq;
    HashFn hash;
    DropFn drop;
} HashMapKeyProps;

typedef struct
{
    size_t size;
    DropFn drop;
} HashMapValueProps;

typedef struct
{
    size_t length;
    size_t capacity;
    void *entries;
    HashMapKeyProps key_props;
    HashMapValueProps value_props;
    Hasher hasher;
} HashMap;

void *_HashMapEntry_key(_HashMapEntry *this)
{
    return (uint8_t *)(this) + ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(_HashMapEntry));
}

void *_HashMapEntry_value(_HashMapEntry *this, const HashMap *map)
{
    return (uint8_t *)(_HashMapEntry_key(this)) + ROUND_SIZE_UP_TO_MAX_ALIGN(map->key_props.size);
}

void _HashMapEntry_new(_HashMapEntry *this, const void *key, const void *value, const HashMap *map)
{
    memcpy(_HashMapEntry_key(this), key, map->key_props.size);
    memcpy(_HashMapEntry_value(this, map), value, map->value_props.size);

    this->is_empty = false;
    this->probe_length = 0;
}

size_t _HashMap_entry_size(const HashMap *this)
{
    return ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(_HashMapEntry)) + ROUND_SIZE_UP_TO_MAX_ALIGN(this->key_props.size) + ROUND_SIZE_UP_TO_MAX_ALIGN(this->value_props.size);
}

_HashMapEntry *_HashMap_entry_at(const HashMap *this, size_t i)
{
    return (_HashMapEntry *)((uint8_t *)(this->entries) + (i * _HashMap_entry_size(this)));
}

void HashMap_with_capacity_and_hasher(HashMap *this, const HashMapKeyProps *key_props, const HashMapValueProps *value_props, size_t capacity, const Hasher *hasher)
{
    this->key_props = *key_props;
    this->value_props = *value_props;
    this->hasher = *hasher;

    this->length = 0;

    this->capacity = capacity;
    const size_t entry_size = _HashMap_entry_size(this);
    this->entries = malloc(this->capacity * entry_size);

    for (size_t i = 0; i < this->capacity; i++)
    {
        _HashMap_entry_at(this, i)->is_empty = true;
    }
}

size_t HashMap_len(const HashMap *this)
{
    return this->length;
}

size_t HashMap_capacity(const HashMap *this)
{
    return this->capacity;
}

void HashMap_insert(HashMap *this, void *key, void *value);

static void _HashMap_grow(HashMap *this)
{
    void *old_entries = this->entries;
    size_t old_length = this->length;

    HashMap_with_capacity_and_hasher(this, &this->key_props, &this->value_props, this->capacity * 2, &this->hasher);

    size_t i = 0;

    while (old_length > 0)
    {
        _HashMapEntry *entry = (_HashMapEntry *)((uint8_t *)(old_entries) + (i * _HashMap_entry_size(this)));

        if (!entry->is_empty)
        {
            HashMap_insert(this, _HashMapEntry_key(entry), _HashMapEntry_value(entry, this));
            old_length--;
        }
    }

    free(old_entries);
}

static float _HashMap_load_factor(const HashMap *this)
{
    return this->length / (float)(this->capacity);
}

static void _HashMap_swap(const HashMap *this, _HashMapEntry *a, _HashMapEntry *b)
{
    const size_t entry_size = _HashMap_entry_size(this);
    uint8_t tmp[entry_size];

    memcpy(tmp, a, entry_size);
    memcpy(a, b, entry_size);
    memcpy(b, tmp, entry_size);
}

void HashMap_insert(HashMap *this, void *key, void *value)
{
    if (_HashMap_load_factor(this) > 0.7)
    {
        _HashMap_grow(this);
    }

    Hasher_reset(&this->hasher);
    this->key_props.hash(key, &this->hasher);

    size_t slot = Hasher_finish(&this->hasher) % this->capacity;

    uint8_t entry_buffer[_HashMap_entry_size(this)];
    _HashMapEntry *entry = (_HashMapEntry *)entry_buffer;

    _HashMapEntry_new(entry, key, value, this);

    while (true)
    {
        _HashMapEntry *current_entry = _HashMap_entry_at(this, slot);

        if (current_entry->is_empty)
        {
            memcpy(current_entry, entry, _HashMap_entry_size(this));
            this->length++;
            break;
        }

        if (this->key_props.eq(_HashMapEntry_key(entry), _HashMapEntry_key(current_entry)))
        {
            if (this->key_props.drop != NULL)
            {
                this->key_props.drop(_HashMapEntry_key(current_entry));
            }

            if (this->value_props.drop != NULL)
            {
                this->value_props.drop(_HashMapEntry_value(current_entry, this));
            }

            memcpy(_HashMapEntry_value(current_entry, this), _HashMapEntry_value(entry, this), ROUND_SIZE_UP_TO_MAX_ALIGN(this->value_props.size));
            break;
        }

        if (current_entry->probe_length < entry->probe_length)
        {
            _HashMap_swap(this, entry, current_entry);
        }

        slot = (slot + 1) % this->capacity;
        entry->probe_length++;
    }
}

void HashMap_with_hasher(HashMap *this, const HashMapKeyProps *key_props, const HashMapValueProps *value_props, const Hasher *hasher)
{
    const size_t INITIAL_CAPACITY = 16;
    HashMap_with_capacity_and_hasher(this, key_props, value_props, INITIAL_CAPACITY, hasher);
}

void HashMap_new(HashMap *this, const HashMapKeyProps *key_props, const HashMapValueProps *value_props)
{
    HasherProps props = {
        .size = sizeof(SimpleHasher),
        .reset = (HasherResetFn)SimpleHasher_reset,
        .write = (HasherWriteFn)SimpleHasher_write,
        .finish = (HasherFinishFn)SimpleHasher_finish,
    };
    SimpleHasher hasher = {};

    Hasher _hasher;
    Hasher_new(&_hasher, &props, &hasher);

    HashMap_with_hasher(this, key_props, value_props, &_hasher);
}

size_t _HashMap_get_entry(HashMap *this, void *key)
{
    size_t result = SIZE_MAX;

    Hasher_reset(&this->hasher);
    this->key_props.hash(key, &this->hasher);

    size_t slot = Hasher_finish(&this->hasher) % this->capacity;
    size_t probe_length = 0;

    while (true)
    {
        _HashMapEntry *slot_entry = _HashMap_entry_at(this, slot);

        if (slot_entry->is_empty)
        {
            break;
        }

        if (this->key_props.eq(key, _HashMapEntry_key(slot_entry)))
        {
            result = slot;
            break;
        }

        if (slot_entry->probe_length < probe_length)
        {
            break;
        }

        slot = (slot + 1) % this->capacity;
        probe_length++;
    }

    return result;
}

void *HashMap_get(HashMap *this, void *key)
{
    size_t result = _HashMap_get_entry(this, key);

    if (result != SIZE_MAX)
    {
        return _HashMapEntry_value(_HashMap_entry_at(this, result), this);
    }
    else
    {
        return NULL;
    }
}

void HashMap_remove(HashMap *this, void *key)
{
    size_t slot = _HashMap_get_entry(this, key);
    if (slot == SIZE_MAX)
    {
        return;
    }

    this->key_props.drop(_HashMapEntry_key(_HashMap_entry_at(this, slot)));

    if (this->value_props.drop)
    {
        this->value_props.drop(_HashMapEntry_value(_HashMap_entry_at(this, slot), this));
    }

    this->length--;

    size_t next_slot = (slot + 1) % this->capacity;

    while (true)
    {
        _HashMapEntry *entry = _HashMap_entry_at(this, slot);
        _HashMapEntry *next_entry = _HashMap_entry_at(this, next_slot);

        if (next_entry->is_empty || next_entry->probe_length == 0)
        {
            entry->is_empty = true;
            break;
        }

        memcpy(entry, next_entry, _HashMap_entry_size(this));
        entry->probe_length--;

        slot = next_slot;
        next_slot = (next_slot + 1) % this->capacity;
    }
}

void HashMap_drop(HashMap *this)
{
    for (size_t i = 0; i < this->capacity; i++)
    {
        _HashMapEntry *entry = _HashMap_entry_at(this, i);

        if (!entry->is_empty)
        {
            this->key_props.drop(_HashMapEntry_key(entry));

            if (this->value_props.drop != NULL)
            {
                this->value_props.drop(_HashMapEntry_value(entry, this));
            }
        }
    }

    Hasher_drop(&this->hasher);

    free(this->entries);
}

// [HashSet]

typedef struct
{
    size_t size;
    EqFn eq;
    HashFn hash;
    DropFn drop;
} HashSetElementProps;

typedef struct
{
    HashMap *map;
} HashSet;

void HashSet_with_capacity_and_hasher(HashSet *this, const HashSetElementProps *element_props, size_t capacity, const Hasher *hasher)
{
    this->map = malloc(sizeof(*this->map));

    HashMapKeyProps key_props = {
        .size = element_props->size,
        .eq = element_props->eq,
        .hash = element_props->hash,
        .drop = element_props->drop,
    };
    HashMapValueProps value_props = {
        .size = sizeof(uint8_t),
    };
    HashMap_with_capacity_and_hasher(this->map, &key_props, &value_props, capacity, hasher);
}

void HashSet_with_hasher(HashSet *this, const HashSetElementProps *element_props, const Hasher *hasher)
{
    this->map = malloc(sizeof(*this->map));

    HashMapKeyProps key_props = {
        .size = element_props->size,
        .eq = element_props->eq,
        .hash = element_props->hash,
        .drop = element_props->drop,
    };
    HashMapValueProps value_props = {
        .size = sizeof(uint8_t),
    };
    HashMap_with_hasher(this->map, &key_props, &value_props, hasher);
}

void HashSet_new(HashSet *this, const HashSetElementProps *element_props)
{
    this->map = malloc(sizeof(*this->map));

    HashMapKeyProps key_props = {
        .size = element_props->size,
        .eq = element_props->eq,
        .hash = element_props->hash,
        .drop = element_props->drop,
    };
    HashMapValueProps value_props = {
        .size = sizeof(uint8_t),
    };
    HashMap_new(this->map, &key_props, &value_props);
}

void HashSet_insert(HashSet *this, void *element)
{
    uint8_t i = 0;
    HashMap_insert(this->map, element, &i);
}

size_t HashSet_len(const HashSet *this)
{
    return HashMap_len(this->map);
}

size_t HashSet_capacity(const HashSet *this)
{
    return HashMap_capacity(this->map);
}

bool HashSet_contains(const HashSet *this, void *element)
{
    return HashMap_get(this->map, element) == NULL ? false : true;
}

// [HashSetUnionIter]

typedef enum
{
    HASH_SET_UNION_ITER_PHASE_A,
    HASH_SET_UNION_ITER_PHASE_B,
    HASH_SET_UNION_ITER_PHASE_DONE,
} HashSetUnionIterPhase;

typedef struct
{
    const HashSet *a;
    const HashSet *b;
    HashSetUnionIterPhase phase;
    size_t current;
} HashSetUnionIter;

void HashSetUnionIter_new(HashSetUnionIter *this, const HashSet *a, const HashSet *b)
{
    this->a = a;
    this->b = b;

    this->phase = HASH_SET_UNION_ITER_PHASE_A;
    this->current = 0;
}

void *HashSetUnionIter_next(HashSetUnionIter *this)
{
    if (this->phase == HASH_SET_UNION_ITER_PHASE_DONE)
    {
        return NULL;
    }

    if (this->phase == HASH_SET_UNION_ITER_PHASE_A)
    {
        size_t i;
        for (i = this->current; i < HashSet_capacity(this->a); i++)
        {
            _HashMapEntry *entry = _HashMap_entry_at(this->a->map, i);

            if (!entry->is_empty)
            {
                break;
            }
        }

        if (i == HashSet_capacity(this->a))
        {
            this->phase = HASH_SET_UNION_ITER_PHASE_B;
            this->current = 0;
        }
        else
        {
            this->current = i + 1;
            return _HashMapEntry_key(_HashMap_entry_at(this->a->map, i));
        }
    }

    if (this->phase == HASH_SET_UNION_ITER_PHASE_B)
    {
        size_t i;
        for (i = this->current; i < HashSet_capacity(this->b); i++)
        {
            _HashMapEntry *entry = _HashMap_entry_at(this->b->map, i);

            if (!entry->is_empty && !HashSet_contains(this->a, _HashMapEntry_key(entry)))
            {
                break;
            }
        }

        if (i == HashSet_capacity(this->b))
        {
            this->phase = HASH_SET_UNION_ITER_PHASE_DONE;
        }
        else
        {
            this->current = i + 1;
            return _HashMapEntry_key(_HashMap_entry_at(this->b->map, i));
        }
    }

    return NULL;
}

void HashSetUnionIter_drop(HashSetUnionIter *this)
{
}

// [HashSetIntersectionIter]

typedef struct
{
    const HashSet *a;
    const HashSet *b;
    size_t current;
    bool is_done;
} HashSetIntersectionIter;

void HashSetIntersectionIter_new(HashSetIntersectionIter *this, const HashSet *a, const HashSet *b)
{
    this->a = a;
    this->b = b;

    this->current = 0;
    this->is_done = false;
}

void *HashSetIntersectionIter_next(HashSetIntersectionIter *this)
{
    if (this->is_done)
    {
        return NULL;
    }

    size_t i;
    for (i = this->current; i < HashSet_capacity(this->a); i++)
    {
        _HashMapEntry *entry = _HashMap_entry_at(this->a->map, i);

        if (!entry->is_empty && HashSet_contains(this->b, _HashMapEntry_key(entry)))
        {
            break;
        }
    }

    if (i == HashSet_capacity(this->b))
    {
        this->is_done = true;
        return NULL;
    }
    else
    {
        this->current = i + 1;
        return _HashMapEntry_key(_HashMap_entry_at(this->a->map, i));
    }
}

void HashSetIntersectionIter_drop(HashSetIntersectionIter *this)
{
}

// [HashSetDifferenceIter]

typedef struct
{
    const HashSet *a;
    const HashSet *b;
    size_t current;
    bool is_done;
} HashSetDifferenceIter;

void HashSetDifferenceIter_new(HashSetDifferenceIter *this, const HashSet *a, const HashSet *b)
{
    this->a = a;
    this->b = b;

    this->current = 0;
    this->is_done = false;
}

void *HashSetDifferenceIter_next(HashSetDifferenceIter *this)
{
    if (this->is_done)
    {
        return NULL;
    }

    size_t i;
    for (i = this->current; i < HashSet_capacity(this->a); i++)
    {
        _HashMapEntry *entry = _HashMap_entry_at(this->a->map, i);

        if (!entry->is_empty && !HashSet_contains(this->b, _HashMapEntry_key(entry)))
        {
            break;
        }
    }

    if (i == HashSet_capacity(this->b))
    {
        this->is_done = true;
        return NULL;
    }
    else
    {
        this->current = i + 1;
        return _HashMapEntry_key(_HashMap_entry_at(this->a->map, i));
    }
}

void HashSetDifferenceIter_drop(HashSetDifferenceIter *this)
{
}

// [HashSetSymmetricDifferenceIter]

typedef enum
{
    HASH_SET_SYMMETRIC_DIFFERENCE_ITER_PHASE_A,
    HASH_SET_SYMMETRIC_DIFFERENCE_ITER_PHASE_B,
    HASH_SET_SYMMETRIC_DIFFERENCE_ITER_PHASE_DONE,
} HashSetSymmetricDifferenceIterPhase;

typedef struct
{
    const HashSet *a;
    const HashSet *b;
    HashSetSymmetricDifferenceIterPhase phase;
    size_t current;
} HashSetSymmetricDifferenceIter;

void HashSetSymmetricDifferenceIter_new(HashSetSymmetricDifferenceIter *this, const HashSet *a, const HashSet *b)
{
    this->a = a;
    this->b = b;

    this->phase = HASH_SET_SYMMETRIC_DIFFERENCE_ITER_PHASE_A;
    this->current = 0;
}

void *HashSetSymmetricDifferenceIter_next(HashSetSymmetricDifferenceIter *this)
{
    if (this->phase == HASH_SET_SYMMETRIC_DIFFERENCE_ITER_PHASE_DONE)
    {
        return NULL;
    }

    if (this->phase == HASH_SET_SYMMETRIC_DIFFERENCE_ITER_PHASE_A)
    {
        size_t i;
        for (i = this->current; i < HashSet_capacity(this->a); i++)
        {
            _HashMapEntry *entry = _HashMap_entry_at(this->a->map, i);

            if (!entry->is_empty && !HashSet_contains(this->b, _HashMapEntry_key(entry)))
            {
                break;
            }
        }

        if (i == HashSet_capacity(this->a))
        {
            this->phase = HASH_SET_SYMMETRIC_DIFFERENCE_ITER_PHASE_B;
            this->current = 0;
        }
        else
        {
            this->current = i + 1;
            return _HashMapEntry_key(_HashMap_entry_at(this->a->map, i));
        }
    }

    if (this->phase == HASH_SET_SYMMETRIC_DIFFERENCE_ITER_PHASE_B)
    {
        size_t i;
        for (i = this->current; i < HashSet_capacity(this->b); i++)
        {
            _HashMapEntry *entry = _HashMap_entry_at(this->b->map, i);

            if (!entry->is_empty && !HashSet_contains(this->a, _HashMapEntry_key(entry)))
            {
                break;
            }
        }

        if (i == HashSet_capacity(this->b))
        {
            this->phase = HASH_SET_SYMMETRIC_DIFFERENCE_ITER_PHASE_DONE;
        }
        else
        {
            this->current = i + 1;
            return _HashMapEntry_key(_HashMap_entry_at(this->b->map, i));
        }
    }

    return NULL;
}

void HashSetSymmetricDifferenceIter_drop(HashSetSymmetricDifferenceIter *this)
{
}

HashSetUnionIter HashSet_union(const HashSet *this, const HashSet *other)
{
    HashSetUnionIter iter;
    HashSetUnionIter_new(&iter, this, other);

    return iter;
}

HashSetIntersectionIter HashSet_intersection(const HashSet *this, const HashSet *other)
{
    HashSetIntersectionIter iter;
    HashSetIntersectionIter_new(&iter, this, other);

    return iter;
}

HashSetDifferenceIter HashSet_difference(const HashSet *this, const HashSet *other)
{
    HashSetDifferenceIter iter;
    HashSetDifferenceIter_new(&iter, this, other);

    return iter;
}

HashSetSymmetricDifferenceIter HashSet_symmetric_difference(const HashSet *this, const HashSet *other)
{
    HashSetSymmetricDifferenceIter iter;
    HashSetSymmetricDifferenceIter_new(&iter, this, other);

    return iter;
}

void HashSet_remove(HashSet *this, void *element)
{
    HashMap_remove(this->map, element);
}

void HashSet_drop(HashSet *this)
{
    HashMap_drop(this->map);
    free(this->map);
}

// [Str]

typedef struct
{
    uint8_t *ptr;
    size_t len;
} Str;

Str Str_from_cstr(const char *cstr)
{
    Str result = {
        .ptr = (uint8_t *)cstr,
        .len = strlen(cstr),
    };
    return result;
}

// [StrSearcher]

typedef enum
{
    SEARCH_STEP_MATCH,
    SEARCH_STEP_REJECT,
    SEARCH_STEP_DONE,
} SearchStepKind;

typedef struct
{
    SearchStepKind kind;

    union
    {
        struct
        {
            size_t start;
            size_t end;
        } match;

        struct
        {
            size_t start;
            size_t end;
        } reject;
    };
} SearchStep;

typedef struct
{
    Str haystack;
    Str needle;
    size_t position;
} StrSearcher;

static void StrSearcher_new(StrSearcher *this, Str haystack, Str needle)
{
    this->haystack = haystack;
    this->needle = needle;

    this->position = 0;
}

static Str StrSearcher_haystack(const StrSearcher *this)
{
    Str result = {
        .ptr = this->haystack.ptr,
        .len = this->haystack.len,
    };
    return result;
}

static const uint8_t *_StrSearcher_find_str(const uint8_t *haystack, size_t haystack_len, const uint8_t *needle, size_t needle_len)
{
    if (needle_len == 0)
        return haystack;

    if (needle_len > haystack_len)
        return NULL;

    const uint8_t *end = haystack + (haystack_len - needle_len) + 1;

    for (const uint8_t *h = haystack; h < end; h++)
    {
        if (memcmp(h, needle, needle_len) == 0)
            return h;
    }

    return NULL;
}

static SearchStep StrSearcher_next(StrSearcher *this)
{
    SearchStep result;

    if (this->position == this->haystack.len)
    {
        result.kind = SEARCH_STEP_DONE;
        return result;
    }

    const uint8_t *match = _StrSearcher_find_str(this->haystack.ptr + this->position, this->haystack.len - this->position, this->needle.ptr, this->needle.len);

    if (match == NULL)
    {
        result.kind = SEARCH_STEP_REJECT;
        result.reject.start = this->position;
        result.reject.end = this->haystack.len;

        this->position = this->haystack.len;

        return result;
    }

    size_t match_start = match - this->haystack.ptr;
    size_t match_end = match_start + this->needle.len;

    if (match_start > this->position)
    {
        // There's a rejection region before the match

        result.kind = SEARCH_STEP_REJECT;
        result.reject.start = this->position;
        result.reject.end = match_start;

        this->position = match_start;

        return result;
    }
    else
    {
        result.kind = SEARCH_STEP_MATCH;
        result.match.start = this->position;
        result.match.end = match_end;

        this->position = match_end;

        return result;
    }
}

static void StrSearcher_drop(StrSearcher *this)
{
}

// [MatchesIterator]

typedef struct
{
    size_t start;
    size_t end;
} Match;

typedef struct
{
    StrSearcher searcher;
    bool is_done;
    Match current;
} MatchesIterator;

void MatchesIterator_new(MatchesIterator *this, const StrSearcher *searcher)
{
    memcpy(&this->searcher, searcher, sizeof(StrSearcher));
    this->is_done = false;
}

void *MatchesIterator_next(MatchesIterator *this)
{
    if (this->is_done)
    {
        return NULL;
    }

    while (true)
    {
        SearchStep step = StrSearcher_next(&this->searcher);

        if (step.kind == SEARCH_STEP_DONE)
        {
            this->is_done = true;
            return NULL;
        }

        if (step.kind == SEARCH_STEP_MATCH)
        {
            this->current.start = step.match.start;
            this->current.end = step.match.end;
            return &this->current;
        }
    }
}

void MatchesIterator_drop(MatchesIterator *this)
{
    StrSearcher_drop(&this->searcher);
}

// [SplitIterator]

typedef struct
{
    StrSearcher searcher;
    size_t last_end;
    bool is_done;
    Str current;
} SplitIterator;

void SplitIterator_new(SplitIterator *this, const StrSearcher *searcher)
{
    memcpy(&this->searcher, searcher, sizeof(StrSearcher));
    this->last_end = 0;
    this->is_done = false;
}

void *SplitIterator_next(SplitIterator *this)
{
    if (this->is_done)
    {
        return NULL;
    }

    while (true)
    {
        SearchStep step = StrSearcher_next(&this->searcher);

        if (step.kind == SEARCH_STEP_MATCH)
        {
            size_t start = this->last_end;
            size_t end = step.match.start;

            this->last_end = step.match.end;

            this->current.ptr = StrSearcher_haystack(&this->searcher).ptr + start;
            this->current.len = end - start;

            return &this->current;
        }

        if (step.kind == SEARCH_STEP_DONE)
        {
            size_t start = this->last_end;
            size_t end = StrSearcher_haystack(&this->searcher).len;

            this->is_done = true;

            this->current.ptr = StrSearcher_haystack(&this->searcher).ptr + start;
            this->current.len = end - start;

            return &this->current;
        }
    }
}

void SplitIterator_drop(SplitIterator *this)
{
    StrSearcher_drop(&this->searcher);
}

// [String]

typedef struct
{
    Vec *buffer;
} String;

void String_new(String *this)
{
    this->buffer = malloc(sizeof(Vec));

    VecElementOps element_ops = {};
    Vec_new(this->buffer, sizeof(uint8_t), &element_ops);
}

void String_insert_str(String *this, size_t i, Str str)
{
    if (str.len == 0)
    {
        return;
    }

    size_t spare_length = Vec_capacity(this->buffer) - Vec_len(this->buffer);

    if (str.len > spare_length)
    {
        size_t additional = str.len - spare_length;
        Vec_reserve(this->buffer, additional);
    }

    if (i != Vec_len(this->buffer))
    {
        memmove(Vec_get_mut(this->buffer, i + str.len), Vec_get(this->buffer, i), Vec_len(this->buffer) - i);
    }

    memcpy(Vec_get_mut(this->buffer, i), str.ptr, str.len);

    Vec_set_len(this->buffer, Vec_len(this->buffer) + str.len);
}

Str String_as_str(String *this)
{
    Str result = {
        .ptr = Vec_get_mut(this->buffer, 0),
        .len = Vec_len(this->buffer),
    };
    return result;
}

void String_push_str(String *this, Str str)
{
    String_insert_str(this, Vec_len(this->buffer), str);
}

size_t String_find_str(String *this, Str needle)
{
    StrSearcher searcher;
    StrSearcher_new(&searcher, String_as_str(this), needle);

    size_t result = SIZE_MAX;

    while (true)
    {
        SearchStep step = StrSearcher_next(&searcher);

        if (step.kind == SEARCH_STEP_DONE)
        {
            break;
        }

        if (step.kind == SEARCH_STEP_MATCH)
        {
            result = step.match.start;
            break;
        }
    }

    return result;
}

String String_replace_str(String *this, Str from, Str to)
{
    StrSearcher searcher;
    StrSearcher_new(&searcher, String_as_str(this), from);

    String result;
    String_new(&result);

    while (true)
    {
        SearchStep step = StrSearcher_next(&searcher);

        if (step.kind == SEARCH_STEP_MATCH)
        {
            String_push_str(&result, to);
        }
        else if (step.kind == SEARCH_STEP_REJECT)
        {
            Str str = {
                .ptr = Vec_get_mut(this->buffer, 0) + step.reject.start,
                .len = step.reject.end - step.reject.start,
            };
            String_push_str(&result, str);
        }
        else if (step.kind == SEARCH_STEP_DONE)
        {
            break;
        }
    }

    return result;
}

MatchesIterator String_matches_str(String *this, Str needle)
{
    StrSearcher searcher;
    StrSearcher_new(&searcher, String_as_str(this), needle);

    MatchesIterator matches;
    MatchesIterator_new(&matches, &searcher);

    return matches;
}

SplitIterator String_split_str(String *this, Str separator)
{
    StrSearcher searcher;
    StrSearcher_new(&searcher, String_as_str(this), separator);

    SplitIterator split;
    SplitIterator_new(&split, &searcher);

    return split;
}

void String_from(String *this, Str str)
{
    String_new(this);

    String_insert_str(this, 0, str);
}

void String_reserve(String *this, size_t additional)
{
    Vec_reserve(this->buffer, additional);
}

size_t String_len(const String *this)
{
    return Vec_len(this->buffer);
}

uint8_t String_get(const String *this, size_t i)
{
    return *(uint8_t *)Vec_get(this->buffer, i);
}

size_t String_capacity(const String *this)
{
    return Vec_capacity(this->buffer);
}

void String_truncate(String *this, size_t len)
{
    Vec_truncate(this->buffer, len);
}

void String_shrink_to_fit(String *this)
{
    Vec_shrink_to_fit(this->buffer);
}

void String_clear(String *this)
{
    Vec_clear(this->buffer);
}

void String_drop(String *this)
{
    Vec_drop(this->buffer);
    free(this->buffer);
}

// [Regex]

#include <stdint.h>

typedef enum
{
    TOKEN_KIND_LITERAL,
    TOKEN_KIND_CONCATENATION,
    TOKEN_KIND_ALTERNATION,
    TOKEN_KIND_ZERO_OR_ONE,
    TOKEN_KIND_LAZY_ZERO_OR_ONE,
    TOKEN_KIND_ZERO_OR_MORE,
    TOKEN_KIND_LAZY_ZERO_OR_MORE,
    TOKEN_KIND_ONE_OR_MORE,
    TOKEN_KIND_LAZY_ONE_OR_MORE,
    TOKEN_KIND_LEFT_PARENS,
    TOKEN_KIND_RIGHT_PARENS,
} TokenKind;

typedef struct
{
    TokenKind kind;

    union
    {
        struct
        {
            uint8_t c;
        } literal;
    };
} Token;

typedef enum
{
    STATE_KIND_LITERAL,
    STATE_KIND_SPLIT,
    STATE_KIND_TAG,
    STATE_KIND_MATCH,
} StateKind;

typedef struct _State
{
    StateKind kind;

    union
    {
        struct
        {
            uint8_t c;
            struct _State *out;
        } literal;

        struct
        {
            struct _State *out1;
            struct _State *out2;
        } split;

        struct
        {
            size_t id;
            bool is_start;
            struct _State *out;
        } tag;
    };
} State;

typedef State **OutList;

OutList OutList_new(State **out)
{
    *out = NULL;
    return out;
}

OutList OutList_append(OutList this, OutList other)
{
    OutList current;
    for (current = this; *current != NULL; current = (OutList)*current)
        ;

    *current = (State *)other;

    return this;
}

void OutList_patch(OutList this, State *to)
{
    for (OutList current = this; current != NULL;)
    {
        OutList next = (OutList)*current;
        *current = to;
        current = next;
    }
}

typedef struct
{
    State *start;
    OutList out_list;
} Fragment;

typedef struct
{
    Vec states;
    State *start;
    size_t tag_count;
    size_t match_idx;
} NFA;

bool NFA_can_start_expr(const Token *token)
{
    switch (token->kind)
    {
    case TOKEN_KIND_LITERAL:
    case TOKEN_KIND_LEFT_PARENS:
        return true;

    default:
        return false;
    }
}

bool NFA_can_end_expr(const Token *token)
{
    switch (token->kind)
    {
    case TOKEN_KIND_LITERAL:
    case TOKEN_KIND_RIGHT_PARENS:
    case TOKEN_KIND_ZERO_OR_ONE:
    case TOKEN_KIND_LAZY_ZERO_OR_ONE:
    case TOKEN_KIND_ZERO_OR_MORE:
    case TOKEN_KIND_LAZY_ZERO_OR_MORE:
    case TOKEN_KIND_ONE_OR_MORE:
    case TOKEN_KIND_LAZY_ONE_OR_MORE:
        return true;

    default:
        return false;
    }
}

size_t NFA_get_oper_precedence(const Token *token)
{
    switch (token->kind)
    {
    case TOKEN_KIND_ZERO_OR_ONE:
    case TOKEN_KIND_LAZY_ZERO_OR_ONE:
    case TOKEN_KIND_ZERO_OR_MORE:
    case TOKEN_KIND_LAZY_ZERO_OR_MORE:
    case TOKEN_KIND_ONE_OR_MORE:
    case TOKEN_KIND_LAZY_ONE_OR_MORE:
        return 3;

    case TOKEN_KIND_CONCATENATION:
        return 2;

    case TOKEN_KIND_ALTERNATION:
        return 1;

    default:
        assert(false);
        return 0;
    }
}

Vec NFA_to_postfix(Vec *tokens)
{
    VecDeque stack;
    VecDeque_new(&stack, &(VecDequeElementProps){.size = sizeof(Token)});

    Vec postfix;
    Vec_new(&postfix, sizeof(Token), &(VecElementOps){});

    for (size_t i = 0; i < Vec_len(tokens); i++)
    {
        const Token *token = Vec_get(tokens, i);

        switch (token->kind)
        {
        case TOKEN_KIND_LITERAL:
            Vec_push(&postfix, token);
            break;

        case TOKEN_KIND_LEFT_PARENS:
        {
            Vec_push(&postfix, token);
            VecDeque_push_back(&stack, token);
        }
        break;

        case TOKEN_KIND_RIGHT_PARENS:
        {
            const Token *back = VecDeque_back(&stack);

            while (back != NULL && back->kind != TOKEN_KIND_LEFT_PARENS)
            {
                Vec_push(&postfix, back);

                VecDeque_pop_back(&stack);
                back = VecDeque_back(&stack);
            }

            VecDeque_pop_back(&stack);

            Vec_push(&postfix, token);
        }
        break;

        default:
        {
            for (const Token *back = VecDeque_back(&stack); back != NULL && back->kind != TOKEN_KIND_LEFT_PARENS; back = VecDeque_back(&stack))
            {
                if ((NFA_get_oper_precedence(back) < NFA_get_oper_precedence(token)))
                {
                    break;
                }

                Vec_push(&postfix, back);

                VecDeque_pop_back(&stack);
            }

            VecDeque_push_back(&stack, token);
        }
        }
    }

    for (const Token *back = VecDeque_back(&stack); back != NULL; back = VecDeque_back(&stack))
    {
        Vec_push(&postfix, back);

        VecDeque_pop_back(&stack);
    }

    VecDeque_drop(&stack);

    return postfix;
}

Vec NFA_tokenize(const String *regex)
{
    Vec tokens;
    Vec_new(&tokens, sizeof(Token), &(VecElementOps){});

    for (size_t i = 0; i < String_len(regex); i++)
    {
        switch (String_get(regex, i))
        {
        case '|':
            Vec_push(&tokens, &(Token){.kind = TOKEN_KIND_ALTERNATION});
            break;

        case '?':
        {
            bool is_lazy = i + 1 < String_len(regex) && String_get(regex, i + 1) == '?';

            if (is_lazy)
            {
                Vec_push(&tokens, &(Token){.kind = TOKEN_KIND_LAZY_ZERO_OR_ONE});
                i += 1;
            }
            else
            {
                Vec_push(&tokens, &(Token){.kind = TOKEN_KIND_ZERO_OR_ONE});
            }
        }
        break;

        case '*':
        {
            bool is_lazy = i + 1 < String_len(regex) && String_get(regex, i + 1) == '?';

            if (is_lazy)
            {
                Vec_push(&tokens, &(Token){.kind = TOKEN_KIND_LAZY_ZERO_OR_MORE});
                i += 1;
            }
            else
            {
                Vec_push(&tokens, &(Token){.kind = TOKEN_KIND_ZERO_OR_MORE});
            }
        }
        break;

        case '+':
        {
            bool is_lazy = i + 1 < String_len(regex) && String_get(regex, i + 1) == '?';

            if (is_lazy)
            {
                Vec_push(&tokens, &(Token){.kind = TOKEN_KIND_LAZY_ONE_OR_MORE});
                i += 1;
            }
            else
            {
                Vec_push(&tokens, &(Token){.kind = TOKEN_KIND_ONE_OR_MORE});
            }
        }
        break;

        case '(':
            Vec_push(&tokens, &(Token){.kind = TOKEN_KIND_LEFT_PARENS});
            break;

        case ')':
            Vec_push(&tokens, &(Token){.kind = TOKEN_KIND_RIGHT_PARENS});
            break;

        case '\\':
        {
            i += 1;

            Vec_push(
                &tokens,
                &(Token){
                    .kind = TOKEN_KIND_LITERAL,
                    .literal = {
                        .c = String_get(regex, i),
                    },
                });
        }
        break;

        default:
        {
            Vec_push(
                &tokens,
                &(Token){
                    .kind = TOKEN_KIND_LITERAL,
                    .literal = {
                        .c = String_get(regex, i),
                    },
                });
        }
        }
    }

    // insert concatenation markers

    for (size_t i = 1; i < Vec_len(&tokens); i++)
    {
        if (NFA_can_end_expr(Vec_get(&tokens, i - 1)) && NFA_can_start_expr(Vec_get(&tokens, i)))
        {
            Vec_insert(&tokens, i, &(Token){.kind = TOKEN_KIND_CONCATENATION});
            i += 1;
        }
    }

    Vec postfix = NFA_to_postfix(&tokens);

    Vec_drop(&tokens);

    return postfix;
}

NFA NFA_from_str(const String *str)
{
    Vec tokens = NFA_tokenize(str);

    Vec states;
    Vec_with_capacity(&states, sizeof(State), &(VecElementOps){}, Vec_len(&tokens) + 1);

    VecDeque fragment_stack;
    VecDeque_new(&fragment_stack, &(VecDequeElementProps){.size = sizeof(Fragment)});

    size_t tag_count = 0;

    for (size_t i = 0; i < Vec_len(&tokens); i++)
    {
        const Token *token = Vec_get(&tokens, i);

        switch (token->kind)
        {
        case TOKEN_KIND_LITERAL:
        {
            Vec_push(
                &states,
                &(State){
                    .kind = STATE_KIND_LITERAL,
                    .literal = {
                        .c = token->literal.c,
                    },
                });

            State *new = Vec_get_mut(&states, Vec_len(&states) - 1);

            VecDeque_push_back(
                &fragment_stack,
                &(Fragment){
                    .start = new,
                    .out_list = OutList_new((State **)&new->literal.out),
                });
        }
        break;

        case TOKEN_KIND_LEFT_PARENS:
        {
            Vec_push(
                &states,
                &(State){
                    .kind = STATE_KIND_TAG,
                    .tag = {
                        .id = tag_count,
                        .is_start = true,
                    },
                });

            tag_count += 1;

            State *new = Vec_get_mut(&states, Vec_len(&states) - 1);

            VecDeque_push_back(
                &fragment_stack,
                &(Fragment){
                    .start = new,
                    .out_list = OutList_new((State **)&new->tag.out),
                });
        }
        break;

        case TOKEN_KIND_RIGHT_PARENS:
        {
            Fragment frag = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            Fragment start_tag = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            OutList_patch(start_tag.out_list, frag.start);

            Vec_push(
                &states,
                &(State){
                    .kind = STATE_KIND_TAG,
                    .tag = {
                        .id = start_tag.start->tag.id,
                        .is_start = false,
                    },
                });

            State *new = Vec_get_mut(&states, Vec_len(&states) - 1);

            OutList_patch(frag.out_list, new);

            VecDeque_push_back(
                &fragment_stack,
                &(Fragment){
                    .start = start_tag.start,
                    .out_list = OutList_new((State **)&new->tag.out),
                });
        }
        break;

        case TOKEN_KIND_CONCATENATION:
        {
            Fragment frag2 = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            Fragment frag1 = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            OutList_patch(frag1.out_list, frag2.start);

            VecDeque_push_back(
                &fragment_stack,
                &(Fragment){
                    .start = frag1.start,
                    .out_list = frag2.out_list,
                });
        }
        break;

        case TOKEN_KIND_ALTERNATION:
        {
            Fragment frag2 = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            Fragment frag1 = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            Vec_push(
                &states,
                &(State){
                    .kind = STATE_KIND_SPLIT,
                    .split = {
                        .out1 = frag1.start,
                        .out2 = frag2.start,
                    },
                });

            State *new = Vec_get_mut(&states, Vec_len(&states) - 1);

            VecDeque_push_back(
                &fragment_stack,
                &(Fragment){
                    .start = new,
                    .out_list = OutList_append(frag1.out_list, frag2.out_list),
                });
        }
        break;

        case TOKEN_KIND_ZERO_OR_ONE:
        {
            Fragment frag = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            Vec_push(
                &states,
                &(State){
                    .kind = STATE_KIND_SPLIT,
                    .split = {
                        .out1 = frag.start,
                    },
                });

            State *new = Vec_get_mut(&states, Vec_len(&states) - 1);

            VecDeque_push_back(
                &fragment_stack,
                &(Fragment){
                    .start = new,
                    .out_list = OutList_append(frag.out_list, OutList_new(&new->split.out2)),
                });
        }
        break;

        case TOKEN_KIND_LAZY_ZERO_OR_ONE:
        {
            Fragment frag = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            Vec_push(
                &states,
                &(State){
                    .kind = STATE_KIND_SPLIT,
                    .split = {
                        .out2 = frag.start,
                    },
                });

            State *new = Vec_get_mut(&states, Vec_len(&states) - 1);

            VecDeque_push_back(
                &fragment_stack,
                &(Fragment){
                    .start = new,
                    .out_list = OutList_append(frag.out_list, OutList_new(&new->split.out1)),
                });
        }
        break;

        case TOKEN_KIND_ZERO_OR_MORE:
        {
            Fragment frag = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            Vec_push(
                &states,
                &(State){
                    .kind = STATE_KIND_SPLIT,
                    .split = {
                        .out1 = frag.start,
                    },
                });

            State *new = Vec_get_mut(&states, Vec_len(&states) - 1);

            OutList_patch(frag.out_list, new);

            VecDeque_push_back(
                &fragment_stack,
                &(Fragment){
                    .start = new,
                    .out_list = OutList_new(&new->split.out2),
                });
        }
        break;

        case TOKEN_KIND_LAZY_ZERO_OR_MORE:
        {
            Fragment frag = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            Vec_push(
                &states,
                &(State){
                    .kind = STATE_KIND_SPLIT,
                    .split = {
                        .out2 = frag.start,
                    },
                });

            State *new = Vec_get_mut(&states, Vec_len(&states) - 1);

            OutList_patch(frag.out_list, new);

            VecDeque_push_back(
                &fragment_stack,
                &(Fragment){
                    .start = new,
                    .out_list = OutList_new(&new->split.out1),
                });
        }
        break;

        case TOKEN_KIND_ONE_OR_MORE:
        {
            Fragment frag = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            Vec_push(
                &states,
                &(State){
                    .kind = STATE_KIND_SPLIT,
                    .split = {
                        .out1 = frag.start,
                    },
                });

            State *new = Vec_get_mut(&states, Vec_len(&states) - 1);

            OutList_patch(frag.out_list, new);

            VecDeque_push_back(
                &fragment_stack,
                &(Fragment){
                    .start = frag.start,
                    .out_list = OutList_new(&new->split.out2),
                });
        }
        break;

        case TOKEN_KIND_LAZY_ONE_OR_MORE:
        {
            Fragment frag = *(Fragment *)VecDeque_back(&fragment_stack);
            VecDeque_pop_back(&fragment_stack);

            Vec_push(
                &states,
                &(State){
                    .kind = STATE_KIND_SPLIT,
                    .split = {
                        .out2 = frag.start,
                    },
                });

            State *new = Vec_get_mut(&states, Vec_len(&states) - 1);

            OutList_patch(frag.out_list, new);

            VecDeque_push_back(
                &fragment_stack,
                &(Fragment){
                    .start = frag.start,
                    .out_list = OutList_new(&new->split.out1),
                });
        }
        break;

        default:
            assert(false);
        }
    }

    Vec_push(&states, &(State){.kind = STATE_KIND_MATCH});

    size_t match_idx = Vec_len(&states) - 1;
    State *new = Vec_get_mut(&states, match_idx);
    Fragment frag = *(Fragment *)VecDeque_back(&fragment_stack);
    OutList_patch(frag.out_list, new);

    return (NFA){
        .states = states,
        .start = frag.start,
        .tag_count = tag_count,
        .match_idx = match_idx,
    };
}

size_t NFA_state_idx(NFA *this, State *state)
{
    return ((uint8_t *)(state) - (uint8_t *)(Vec_get(&this->states, 0))) / this->states.element_size;
}

void NFA_drop(NFA *this)
{
    Vec_drop(&this->states);
}

typedef struct
{
    NFA nfa;
} Regex;

Regex Regex_new(const String *regex)
{
    return (Regex){
        .nfa = NFA_from_str(regex),
    };
}

typedef struct
{
    size_t start;
    size_t end;
} Capture;

typedef struct
{
    uint8_t *splits;
    size_t split_count;
    size_t split_capacity;
    Capture *tags;
    size_t tag_capacity;
} ThreadHistory;

typedef struct
{
    State *state;
    ThreadHistory history;
} Thread;

Thread Thread_new(State *state, size_t tag_capacity)
{
    Thread this = {
        .state = state,
    };

    this.history.split_count = 0;
    this.history.split_capacity = 64;
    this.history.splits = malloc(this.history.split_capacity * sizeof(this.history.splits));

    this.history.tag_capacity = tag_capacity;

    if (tag_capacity)
    {
        this.history.tags = malloc(this.history.tag_capacity * sizeof(*this.history.tags));

        for (size_t i = 0; i < this.history.tag_capacity; i++)
        {
            this.history.tags[i].start = SIZE_MAX;
            this.history.tags[i].end = SIZE_MAX;
        }
    }

    return this;
}

Thread Thread_clone(const Thread *other)
{
    Thread this = {
        .state = other->state,
        .history = {
            .split_count = other->history.split_count,
            .split_capacity = other->history.split_capacity,
            .tag_capacity = other->history.tag_capacity,
        },
    };

    this.history.splits = malloc(this.history.split_capacity * sizeof(this.history.splits));
    memcpy(this.history.splits, other->history.splits, other->history.split_count * sizeof(this.history.splits));

    if (this.history.tag_capacity)
    {
        this.history.tags = malloc(this.history.tag_capacity * sizeof(*this.history.tags));
        memcpy(this.history.tags, other->history.tags, other->history.tag_capacity * sizeof(*this.history.tags));
    }

    return this;
}

void Thread_push_split(Thread *this, uint8_t split)
{
    if (this->history.split_count == this->history.split_capacity)
    {
        this->history.split_capacity *= 2;
        this->history.splits = realloc(this->history.splits, this->history.split_capacity * sizeof(*this->history.splits));
    }

    this->history.splits[this->history.split_count] = split;
    this->history.split_count += 1;
}

bool Thread_new_is_better(const Thread *this, const Thread *new)
{
    for (size_t i = 0; i < this->history.split_count; i++)
    {
        if (new->history.splits[i] > this->history.splits[i])
        {
            return true;
        }
        else if (new->history.splits[i] < this->history.splits[i])
        {
            return false;
        }
    }

    // prevent cycles as in (.?)*:
    //
    // Split₀
    // ├── out1 → Split₁
    // │           ├── out1 → '.' → back to Split₀
    // │           └── out2 → back to Split₀
    // └── out2 → Match
    //
    // when the e-closure cycles back to the original state, it will have the original state's history as it's prefix. We see this and keep
    // the original state. Since the e-closure computes the concrete states reachable from the original state and it is already being processed
    // earlier in the e-closure, we can simply discard the recursive call

    return false;
}

void Thread_set_tag(Thread *this, size_t id, bool is_start, size_t pos)
{
    if (is_start)
    {
        this->history.tags[id].start = pos;
    }
    else
    {
        this->history.tags[id].end = pos;
    }
}

void Thread_drop(Thread *this)
{
    free(this->history.splits);

    if (this->history.tag_capacity)
    {
        free(this->history.tags);
    }
}

void Regex_e_closure(Regex *this, size_t pos, Thread *new, Vec *visited, Vec *list)
{
    size_t idx = NFA_state_idx(&this->nfa, new->state);
    uint8_t *already_visited = (uint8_t *)Vec_get_mut(visited, idx);

    if (*already_visited == 1)
    {
        Thread *old = (Thread *)Vec_get_mut(list, idx);

        if (Thread_new_is_better(old, new))
        {
            Thread_drop(old);
        }
        else
        {
            Thread_drop(new);
            return;
        }
    }

    *already_visited = 1;
    *(Thread *)Vec_get_mut(list, idx) = *new;

    switch (new->state->kind)
    {
    case STATE_KIND_SPLIT:
    {
        Thread t1 = Thread_clone(new);
        t1.state = new->state->split.out1;
        Thread_push_split(&t1, 1);
        Regex_e_closure(this, pos, &t1, visited, list);

        Thread t2 = Thread_clone(new);
        t2.state = new->state->split.out2;
        Thread_push_split(&t2, 0);
        Regex_e_closure(this, pos, &t2, visited, list);
    }
    break;

    case STATE_KIND_TAG:
    {
        Thread t = Thread_clone(new);
        t.state = new->state->tag.out;
        Thread_set_tag(&t, new->state->tag.id, new->state->tag.is_start, pos);
        Regex_e_closure(this, pos, &t, visited, list);
    }
    break;

    default:
        break;
    }
}

void Regex_step(Regex *this, size_t pos, uint8_t c, Vec *current_visited, Vec *current_list, Vec *next_visited, Vec *next_list)
{
    memset(Vec_get_mut(next_visited, 0), 0, Vec_len(next_visited) * sizeof(uint8_t));

    for (size_t i = 0; i < Vec_len(current_visited); i++)
    {
        if (*(uint8_t *)Vec_get(current_visited, i) == 1)
        {
            Thread *t = (Thread *)Vec_get(current_list, i);

            if (t->state->kind == STATE_KIND_LITERAL && c == t->state->literal.c)
            {
                Thread new = Thread_clone(t);
                new.state = t->state->literal.out;
                Regex_e_closure(this, pos, &new, next_visited, next_list);
            }

            Thread_drop(t);
        }
    }

    SWAP(*current_list, *next_list);
    SWAP(*current_visited, *next_visited);
}

bool Regex_match(Regex *this, const String *str, Vec *captures)
{
    size_t state_count = Vec_len(&this->nfa.states);

    Vec current_list;
    Vec_with_capacity(&current_list, sizeof(Thread), &(VecElementOps){}, state_count);
    Vec_set_len(&current_list, state_count);

    Vec current_visited;
    Vec_with_capacity(&current_visited, sizeof(uint8_t), &(VecElementOps){}, state_count);
    Vec_set_len(&current_visited, state_count);

    Vec next_list;
    Vec_with_capacity(&next_list, sizeof(Thread), &(VecElementOps){}, state_count);
    Vec_set_len(&next_list, state_count);

    Vec next_visited;
    Vec_with_capacity(&next_visited, sizeof(uint8_t), &(VecElementOps){}, state_count);
    Vec_set_len(&next_visited, state_count);

    memset(Vec_get_mut(&current_visited, 0), 0, Vec_len(&current_visited) * sizeof(uint8_t));

    Thread start = Thread_new(this->nfa.start, this->nfa.tag_count);
    Regex_e_closure(this, 0, &start, &current_visited, &current_list);

    for (size_t i = 0; i < String_len(str); i++)
    {
        uint8_t c = String_get(str, i);
        Regex_step(this, i + 1, c, &current_visited, &current_list, &next_visited, &next_list);

        bool has_any = false;
        for (size_t j = 0; j < Vec_len(&current_visited); j++)
        {
            if (*(uint8_t *)Vec_get(&current_visited, j) == 1)
            {
                has_any = true;
                break;
            }
        }

        if (!has_any)
        {
            break;
        }
    }

    bool has_match = *(uint8_t *)Vec_get(&current_visited, this->nfa.match_idx) == 1;

    if (has_match && this->nfa.tag_count != 0)
    {
        Vec_with_capacity(captures, sizeof(Capture), &(VecElementOps){}, this->nfa.tag_count);

        const Thread *thread = Vec_get(&current_list, this->nfa.match_idx);

        for (size_t j = 0; j < this->nfa.tag_count; j++)
        {
            Vec_push(captures, &thread->history.tags[j]);
        }
    }

    Vec_drop(&current_list);
    Vec_drop(&current_visited);
    Vec_drop(&next_list);
    Vec_drop(&next_visited);

    return has_match;
}

void Regex_drop(Regex *this)
{
    NFA_drop(&this->nfa);
}

bool eq_u8(const uint8_t *a, const uint8_t *b)
{
    return *a == *b;
}

void hash_u8(const uint8_t *key, Hasher *hasher)
{
    Hasher_write(hasher, key, sizeof(uint8_t));
}

int32_t compare_u8(const uint8_t *a, const uint8_t *b)
{
    if (*a > *b)
    {
        return 1;
    }
    else if (*a == *b)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

void drop_nop(void *ptr)
{
    (void)ptr;
}

int main(int argc, const char **argv)
{
    {
        Vec u8vec;
        Vec_new(&u8vec, sizeof(uint8_t), NULL);

        assert(Vec_capacity(&u8vec) == 0);
        assert(Vec_len(&u8vec) == 0);

        uint8_t u8value = 1;
        Vec_push(&u8vec, &u8value);
        u8value = 2;
        Vec_push(&u8vec, &u8value);
        u8value = 3;
        Vec_push(&u8vec, &u8value);

        assert(Vec_len(&u8vec) == 3);
        assert(Vec_capacity(&u8vec) > 3);

        for (size_t i = 0; i < Vec_len(&u8vec); i++)
        {
            uint8_t *element = (uint8_t *)Vec_get_mut(&u8vec, i);

            *element = *element * 2;

            uint8_t new_element = 0;

            Vec_insert(&u8vec, i, &new_element);
            Vec_remove(&u8vec, i);
        }

        assert(Vec_len(&u8vec) == 3);
        assert(*(uint8_t *)Vec_get(&u8vec, 0) == 2);
        assert(*(uint8_t *)Vec_get(&u8vec, 1) == 4);
        assert(*(uint8_t *)Vec_get(&u8vec, 2) == 6);

        size_t previous_capacity = Vec_capacity(&u8vec);
        Vec_clear(&u8vec);
        assert(Vec_len(&u8vec) == 0);
        assert(Vec_capacity(&u8vec) == previous_capacity);

        Vec_shrink_to_fit(&u8vec);
        assert(Vec_capacity(&u8vec) == 0);

        Vec_drop(&u8vec);

        Vec_with_capacity(&u8vec, sizeof(uint8_t), NULL, 3);
        assert(Vec_capacity(&u8vec) == 3);

        Vec_reserve(&u8vec, 3);
        assert(Vec_capacity(&u8vec) == 3);

        u8value = 1;
        Vec_push(&u8vec, &u8value);
        u8value = 2;
        Vec_push(&u8vec, &u8value);
        u8value = 3;
        Vec_push(&u8vec, &u8value);

        Vec_reserve(&u8vec, 3);
        assert(Vec_capacity(&u8vec) == 6);

        Vec_truncate(&u8vec, 1);
        assert(Vec_len(&u8vec) == 1);

        Vec_drop(&u8vec);
    }

    {
        BTreeMap u8u8map;
        BTreeMapKeyProps key_props = {
            .size = sizeof(uint8_t),
            .cmp = (CmpFn)compare_u8,
        };
        BTreeMapValueProps value_props = {
            .size = sizeof(uint8_t),
        };
        BTreeMap_new(&u8u8map, &key_props, &value_props);

        for (uint8_t i = 1; i <= 3; i++)
        {
            BTreeMap_insert(&u8u8map, &i, &i);
        }

        for (uint8_t i = 1; i <= 3; i++)
        {
            const void *value = BTreeMap_get(&u8u8map, &i);

            assert(value != NULL);
            assert(*(uint8_t *)value == i);
        }

        uint8_t key = 2, new_value = 42;
        BTreeMap_insert(&u8u8map, &key, &new_value);

        const void *value = BTreeMap_get(&u8u8map, &key);

        assert(value != NULL);
        assert(*(uint8_t *)value == 42);

        // Remove key = 1 (leaf case)
        key = 1;
        BTreeMap_remove(&u8u8map, &key);
        value = BTreeMap_get(&u8u8map, &key);
        assert(value == NULL);

        // Remove key = 2 (internal node candidate)
        key = 2;
        BTreeMap_remove(&u8u8map, &key);
        value = BTreeMap_get(&u8u8map, &key);
        assert(value == NULL);

        // Try removing a non-existent key
        key = 99;
        BTreeMap_remove(&u8u8map, &key);
        value = BTreeMap_get(&u8u8map, &key);
        assert(value == NULL);

        BTreeMap_drop(&u8u8map);

        BTreeMap_new(&u8u8map, &key_props, &value_props);

        // Insert keys 1..=5
        for (uint8_t i = 1; i <= 5; i++)
        {
            BTreeMap_insert(&u8u8map, &i, &i);
        }

        // Create range [2, 4]
        uint8_t start = 2;
        uint8_t end = 4;

        RangeBound lower = RangeBound_included(&start);
        RangeBound upper = RangeBound_included(&end);

        BTreeMapRangeIter iter = BTreeMap_range(&u8u8map, &lower, &upper);

        // Expected sequence: 2, 3, 4
        uint8_t expected = 2;
        for (BTreeMapEntry *entry = BTreeMapRangeIter_next(&iter); entry != NULL; entry = BTreeMapRangeIter_next(&iter))
        {
            uint8_t key = *(uint8_t *)entry->key;
            uint8_t value = *(uint8_t *)entry->value;

            assert(key == expected);
            assert(value == expected);
            expected++;
        }

        BTreeMap_range(&u8u8map, &lower, &upper);

        assert(expected == 5); // ensures we iterated 2,3,4

        // Expected sequence: 4, 3, 2
        expected = 4;
        for (BTreeMapEntry *entry = BTreeMapRangeIter_next_back(&iter); entry != NULL; entry = BTreeMapRangeIter_next_back(&iter))
        {
            uint8_t key = *(uint8_t *)entry->key;
            uint8_t value = *(uint8_t *)entry->value;

            assert(key == expected);
            assert(value == expected);
            expected--;
        }

        // Test with excluded upper bound (2..4)
        lower = RangeBound_included(&start);
        upper = RangeBound_excluded(&end);
        iter = BTreeMap_range(&u8u8map, &lower, &upper);

        expected = 2;
        for (BTreeMapEntry *entry = BTreeMapRangeIter_next(&iter); entry != NULL; entry = BTreeMapRangeIter_next(&iter))
        {
            uint8_t key = *(uint8_t *)entry->key;
            assert(key == expected);
            expected++;
        }

        assert(expected == 4); // iterated 2,3 only

        BTreeMap_drop(&u8u8map);
    }

    {
        BTreeSet set;
        BTreeSetElementProps props = {
            .size = sizeof(uint8_t),
            .cmp = (CmpFn)compare_u8,
            .drop = NULL,
        };
        BTreeSet_new(&set, &props);

        for (uint8_t i = 1; i <= 3; i++)
        {
            BTreeSet_insert(&set, &i);
        }

        for (uint8_t i = 1; i <= 3; i++)
        {
            assert(BTreeSet_contains(&set, &i));
        }

        assert(BTreeSet_len(&set) == 3);

        uint8_t val = 2;
        BTreeSet_remove(&set, &val);
        assert(!BTreeSet_contains(&set, &val));
        assert(BTreeSet_len(&set) == 2);

        BTreeSet_insert(&set, &val);
        assert(BTreeSet_contains(&set, &val));
        assert(BTreeSet_len(&set) == 3);

        uint8_t nonexistent = 99;
        BTreeSet_remove(&set, &nonexistent);
        assert(BTreeSet_len(&set) == 3);

        BTreeSet_drop(&set);

        // BTreeSet_range

        BTreeSet_new(&set, &props);

        for (uint8_t i = 1; i <= 5; i++)
            BTreeSet_insert(&set, &i);

        uint8_t start = 2;
        uint8_t end = 4;
        RangeBound lower = RangeBound_included(&start);
        RangeBound upper = RangeBound_included(&end);

        BTreeSetRangeIter iter = BTreeSet_range(&set, &lower, &upper);

        uint8_t expected = 2;
        for (void *key = BTreeSetRangeIter_next(&iter); key != NULL; key = BTreeSetRangeIter_next(&iter))
        {
            assert(*(uint8_t *)key == expected);
            expected++;
        }
        assert(expected == 5);

        BTreeSetRangeIter_drop(&iter);
        iter = BTreeSet_range(&set, &lower, &upper);

        expected = 4;
        for (void *key = BTreeSetRangeIter_next_back(&iter); key != NULL; key = BTreeSetRangeIter_next_back(&iter))
        {
            assert(*(uint8_t *)key == expected);
            expected--;
        }
        assert(expected == 1);

        BTreeSetRangeIter_drop(&iter);
        BTreeSet_drop(&set);

        // BTreeSet_union

        BTreeSet a;
        BTreeSet b;
        BTreeSet_new(&a, &props);
        BTreeSet_new(&b, &props);

        for (uint8_t i = 1; i <= 3; i++)
            BTreeSet_insert(&a, &i);

        for (uint8_t i = 3; i <= 5; i++)
            BTreeSet_insert(&b, &i);

        BTreeSetUnionIter union_iter = BTreeSet_union(&a, &b);

        uint8_t expected_union[] = {1, 2, 3, 4, 5};
        size_t idx = 0;

        for (void *key = BTreeSetUnionIter_next(&union_iter); key != NULL; key = BTreeSetUnionIter_next(&union_iter))
        {
            assert(*(uint8_t *)(key) == expected_union[idx]);
            idx++;
        }

        assert(idx == 5);
        BTreeSetUnionIter_drop(&union_iter);
        BTreeSet_drop(&a);
        BTreeSet_drop(&b);

        // BTreeSet_intersection

        BTreeSet_new(&a, &props);
        BTreeSet_new(&b, &props);

        for (uint8_t i = 1; i <= 3; i++)
            BTreeSet_insert(&a, &i);
        for (uint8_t i = 3; i <= 5; i++)
            BTreeSet_insert(&b, &i);

        BTreeSetIntersectionIter intersection_iter = BTreeSet_intersection(&a, &b);
        uint8_t expected_intersection[] = {3};
        idx = 0;

        for (void *key = BTreeSetIntersectionIter_next(&intersection_iter); key != NULL; key = BTreeSetIntersectionIter_next(&intersection_iter))
        {
            assert(*(uint8_t *)key == expected_intersection[idx]);
            idx++;
        }

        assert(idx == 1);
        BTreeSetIntersectionIter_drop(&intersection_iter);
        BTreeSet_drop(&a);
        BTreeSet_drop(&b);

        // BTreeSet_difference

        BTreeSet_new(&a, &props);
        BTreeSet_new(&b, &props);

        for (uint8_t i = 1; i <= 3; i++)
            BTreeSet_insert(&a, &i);
        for (uint8_t i = 3; i <= 5; i++)
            BTreeSet_insert(&b, &i);

        BTreeSetDifferenceIter difference_iter = BTreeSet_difference(&a, &b);
        uint8_t expected_difference[] = {1, 2};
        idx = 0;

        for (void *key = BTreeSetDifferenceIter_next(&difference_iter); key != NULL; key = BTreeSetDifferenceIter_next(&difference_iter))
        {
            assert(*(uint8_t *)key == expected_difference[idx]);
            idx++;
        }

        assert(idx == 2);
        BTreeSetDifferenceIter_drop(&difference_iter);
        BTreeSet_drop(&a);
        BTreeSet_drop(&b);

        // BTreeSet_symmetric_difference

        BTreeSet_new(&a, &props);
        BTreeSet_new(&b, &props);

        for (uint8_t i = 1; i <= 3; i++)
            BTreeSet_insert(&a, &i);
        for (uint8_t i = 3; i <= 5; i++)
            BTreeSet_insert(&b, &i);

        BTreeSetSymmetricDifferenceIter symmetric_difference_iter = BTreeSet_symmetric_difference(&a, &b);
        uint8_t expected_symmetric_difference[] = {1, 2, 4, 5};
        idx = 0;

        for (void *key = BTreeSetSymmetricDifferenceIter_next(&symmetric_difference_iter);
             key != NULL;
             key = BTreeSetSymmetricDifferenceIter_next(&symmetric_difference_iter))
        {
            assert(*(uint8_t *)key == expected_symmetric_difference[idx]);
            idx++;
        }

        assert(idx == 4);
        BTreeSetSymmetricDifferenceIter_drop(&symmetric_difference_iter);
        BTreeSet_drop(&a);
        BTreeSet_drop(&b);
    }

    {
        LinkedListElementProps element_props;
        element_props.element_size = sizeof(uint8_t);
        element_props.drop = drop_nop;

        LinkedList list;
        LinkedList_new(&list, &element_props);

        uint8_t value = 10;
        LinkedList_push_front(&list, &value);
        value = 20;
        LinkedList_push_back(&list, &value);
        value = 30;
        LinkedList_push_back(&list, &value);

        assert(LinkedList_len(&list) == 3);

        uint8_t *front = LinkedList_front(&list);
        assert(front && *front == 10);

        uint8_t *back = LinkedList_back(&list);
        assert(back && *back == 30);

        LinkedList_pop_front(&list);
        front = LinkedList_front(&list);
        assert(front && *front == 20);
        assert(LinkedList_len(&list) == 2);

        LinkedList_pop_back(&list);
        back = LinkedList_back(&list);
        assert(back && *back == 20);
        assert(LinkedList_len(&list) == 1);

        LinkedList_pop_back(&list);
        assert(LinkedList_len(&list) == 0);
        assert(LinkedList_front(&list) == NULL);
        assert(LinkedList_back(&list) == NULL);

        value = 10;
        LinkedList_push_back(&list, &value);
        value = 20;
        LinkedList_push_back(&list, &value);

        assert(LinkedList_len(&list) == 2);
        assert(*((uint8_t *)LinkedList_front(&list)) == 10);
        assert(*((uint8_t *)LinkedList_back(&list)) == 20);

        LinkedList_drop(&list);
    }

    {
        VecDeque deque;
        VecDequeElementProps props = {
            .size = sizeof(uint8_t),
            .drop = drop_nop,
        };
        VecDeque_new(&deque, &props);

        assert(VecDeque_len(&deque) == 0);
        assert(VecDeque_capacity(&deque) == 0);

        uint8_t value = 10;
        VecDeque_push_back(&deque, &value);
        value = 20;
        VecDeque_push_back(&deque, &value);
        value = 30;
        VecDeque_push_back(&deque, &value);

        assert(VecDeque_len(&deque) == 3);
        assert(VecDeque_capacity(&deque) >= 3);

        uint8_t *front = VecDeque_front(&deque);
        uint8_t *back = VecDeque_back(&deque);
        assert(front != NULL && *front == 10);
        assert(back != NULL && *back == 30);

        value = 5;
        VecDeque_push_front(&deque, &value);
        assert(VecDeque_len(&deque) == 4);
        assert(*(uint8_t *)VecDeque_front(&deque) == 5);

        VecDeque_pop_back(&deque);
        assert(VecDeque_len(&deque) == 3);
        assert(*(uint8_t *)VecDeque_back(&deque) == 20);

        VecDeque_pop_front(&deque);
        assert(VecDeque_len(&deque) == 2);
        assert(*(uint8_t *)VecDeque_front(&deque) == 10);

        size_t previous_capacity = VecDeque_capacity(&deque);
        VecDeque_shrink_to_fit(&deque);
        assert(VecDeque_capacity(&deque) == VecDeque_len(&deque));
        assert(VecDeque_capacity(&deque) <= previous_capacity);

        VecDeque_clear(&deque);
        assert(VecDeque_len(&deque) == 0);

        VecDeque_drop(&deque);
    }

    {
        BinaryHeap heap;
        BinaryHeapElementProps props = {
            .size = sizeof(uint8_t),
            .drop = drop_nop,
            .cmp = (CmpFn)compare_u8,
        };

        BinaryHeap_new(&heap, &props);

        assert(BinaryHeap_len(&heap) == 0);
        assert(BinaryHeap_peek(&heap) == NULL);

        uint8_t v = 10;
        BinaryHeap_push(&heap, &v);

        v = 5;
        BinaryHeap_push(&heap, &v);

        v = 30;
        BinaryHeap_push(&heap, &v);

        v = 20;
        BinaryHeap_push(&heap, &v);

        assert(BinaryHeap_len(&heap) == 4);

        const uint8_t *top = BinaryHeap_peek(&heap);
        assert(top != NULL && *top == 30);

        BinaryHeap_pop(&heap);
        assert(BinaryHeap_len(&heap) == 3);
        top = BinaryHeap_peek(&heap);
        assert(top != NULL && *top == 20);

        BinaryHeap_pop(&heap);
        assert(BinaryHeap_len(&heap) == 2);
        top = BinaryHeap_peek(&heap);
        assert(top != NULL && *top == 10);

        BinaryHeap_pop(&heap);
        assert(BinaryHeap_len(&heap) == 1);
        top = BinaryHeap_peek(&heap);
        assert(top != NULL && *top == 5);

        BinaryHeap_pop(&heap);
        assert(BinaryHeap_len(&heap) == 0);
        assert(BinaryHeap_peek(&heap) == NULL);

        v = 42;
        BinaryHeap_push(&heap, &v);
        assert(BinaryHeap_len(&heap) == 1);
        top = BinaryHeap_peek(&heap);
        assert(top != NULL && *top == 42);

        BinaryHeap_drop(&heap);
    }

    {
        HashMapKeyProps key_props = {
            .size = sizeof(uint8_t),
            .eq = (EqFn)eq_u8,
            .hash = (HashFn)hash_u8,
            .drop = drop_nop,
        };

        HashMapValueProps value_props = {
            .size = sizeof(uint8_t),
            .drop = drop_nop,
        };

        HashMap map;
        HashMap_new(&map, &key_props, &value_props);

        for (uint8_t i = 1; i <= 3; i++)
        {
            HashMap_insert(&map, &i, &i);
        }

        for (uint8_t i = 1; i <= 3; i++)
        {
            uint8_t *value = HashMap_get(&map, &i);
            assert(value != NULL);
            assert(*value == i);
        }

        uint8_t key = 2;
        uint8_t new_value = 42;
        HashMap_insert(&map, &key, &new_value);

        uint8_t *value = HashMap_get(&map, &key);
        assert(value != NULL);
        assert(*value == 42);

        key = 1;
        HashMap_remove(&map, &key);
        value = HashMap_get(&map, &key);
        assert(value == NULL);

        key = 2;
        HashMap_remove(&map, &key);
        value = HashMap_get(&map, &key);
        assert(value == NULL);

        key = 99;
        HashMap_remove(&map, &key);
        value = HashMap_get(&map, &key);
        assert(value == NULL);

        HashMap_drop(&map);
    }

    {
        HashSetElementProps elem_props = {
            .size = sizeof(uint8_t),
            .eq = (EqFn)eq_u8,
            .hash = (HashFn)hash_u8,
            .drop = drop_nop,
        };

        HashSet set;
        HashSet_new(&set, &elem_props);

        for (uint8_t i = 1; i <= 3; i++)
        {
            HashSet_insert(&set, &i);
        }

        for (uint8_t i = 1; i <= 3; i++)
        {
            assert(HashSet_contains(&set, &i));
        }

        uint8_t not_present = 99;
        assert(!HashSet_contains(&set, &not_present));

        size_t before = HashSet_len(&set);
        uint8_t dup = 2;
        HashSet_insert(&set, &dup);
        assert(HashSet_len(&set) == before);

        uint8_t key = 2;
        HashSet_remove(&set, &key);
        assert(!HashSet_contains(&set, &key));

        HashSet_remove(&set, &not_present);

        assert(HashSet_len(&set) == 2);

        // --- HashSet_union ---
        HashSet other;
        HashSet_new(&other, &elem_props);

        uint8_t a = 3, b = 4, c = 5;
        HashSet_insert(&other, &a);
        HashSet_insert(&other, &b);
        HashSet_insert(&other, &c);

        HashSetUnionIter u = HashSet_union(&set, &other);

        uint8_t union_elems[6];
        (void)union_elems;

        size_t union_count = 0;
        void *key_ptr;
        while ((key_ptr = HashSetUnionIter_next(&u)))
        {
            union_elems[union_count++] = *(uint8_t *)key_ptr;
        }

        // Union should contain {1, 3, 4, 5}
        assert(union_count == 4);

        // --- HashSet_intersection ---
        HashSetIntersectionIter inter = HashSet_intersection(&set, &other);
        uint8_t inter_elems[6];
        size_t inter_count = 0;
        while ((key_ptr = HashSetIntersectionIter_next(&inter)))
        {
            inter_elems[inter_count++] = *(uint8_t *)key_ptr;
        }

        // Only common element is 3
        assert(inter_count == 1);
        assert(inter_elems[0] == 3);

        // --- HashSet_difference ---
        HashSetDifferenceIter diff = HashSet_difference(&set, &other);
        uint8_t diff_elems[6];
        (void)diff_elems;

        size_t diff_count = 0;
        while ((key_ptr = HashSetDifferenceIter_next(&diff)))
        {
            diff_elems[diff_count++] = *(uint8_t *)key_ptr;
        }
        // set = {1, 3}, other = {3, 4, 5}, difference = {1}
        assert(diff_count == 1);
        assert(diff_elems[0] == 1);

        // --- HashSet_symmetric_difference ---
        HashSetSymmetricDifferenceIter sym = HashSet_symmetric_difference(&set, &other);
        uint8_t sym_elems[6];
        (void)sym_elems;

        size_t sym_count = 0;
        while ((key_ptr = HashSetSymmetricDifferenceIter_next(&sym)))
        {
            sym_elems[sym_count++] = *(uint8_t *)key_ptr;
        }

        // sym diff = {1, 4, 5}
        assert(sym_count == 3);

        HashSet_drop(&set);
        HashSet_drop(&other);
    }

    {
        String s;
        String_new(&s);
        assert(String_len(&s) == 0);
        assert(String_capacity(&s) >= 0);
        String_drop(&s);

        String_from(&s, Str_from_cstr("hello"));
        assert(String_len(&s) == 5);
        assert(memcmp(Vec_get(s.buffer, 0), "hello", 5) == 0);
        String_drop(&s);

        String_new(&s);
        String_push_str(&s, Str_from_cstr("abc"));
        assert(String_len(&s) == 3);
        assert(memcmp(Vec_get(s.buffer, 0), "abc", 3) == 0);

        String_push_str(&s, Str_from_cstr("def"));
        assert(String_len(&s) == 6);
        assert(memcmp(Vec_get(s.buffer, 0), "abcdef", 6) == 0);
        String_drop(&s);

        String_from(&s, Str_from_cstr("HelloWorld"));

        String_insert_str(&s, 5, Str_from_cstr(" "));
        assert(memcmp(Vec_get(s.buffer, 0), "Hello World", 11) == 0);

        String_insert_str(&s, 0, Str_from_cstr("C"));
        assert(memcmp(Vec_get(s.buffer, 0), "CHello World", 12) == 0);

        String_insert_str(&s, String_len(&s), Str_from_cstr("!"));
        assert(memcmp(Vec_get(s.buffer, 0), "CHello World!", 13) == 0);
        String_drop(&s);

        String_new(&s);
        String_reserve(&s, 10);
        assert(String_capacity(&s) >= 10);
        assert(String_len(&s) == 0);
        String_drop(&s);

        String_from(&s, Str_from_cstr("abcdef"));
        String_truncate(&s, 3);
        assert(String_len(&s) == 3);
        assert(memcmp(Vec_get(s.buffer, 0), "abc", 3) == 0);
        String_drop(&s);

        String_from(&s, Str_from_cstr("test"));
        String_clear(&s);
        assert(String_len(&s) == 0);
        String_drop(&s);

        String_from(&s, Str_from_cstr("abcdabc"));
        size_t pos = String_find_str(&s, Str_from_cstr("da"));
        assert(pos == 3);
        String_drop(&s);

        String_from(&s, Str_from_cstr("12345"));
        String_reserve(&s, 100);
        size_t before = String_capacity(&s);
        String_shrink_to_fit(&s);
        size_t after = String_capacity(&s);
        assert(after <= before);
        String_drop(&s);

        // --- String_replace_str ---
        {
            String s;
            String_from(&s, Str_from_cstr("the cat sat on the mat"));
            String replaced = String_replace_str(&s, Str_from_cstr("at"), Str_from_cstr("og"));

            assert(memcmp(Vec_get(replaced.buffer, 0), "the cog sog on the mog", 23) == 0);

            String_drop(&replaced);
            String_drop(&s);
        }

        // --- String_matches_str ---
        {
            String s;
            String_from(&s, Str_from_cstr("banana"));
            MatchesIterator matches = String_matches_str(&s, Str_from_cstr("ana"));

            size_t starts[4];
            size_t count = 0;

            Match *m;
            while ((m = (Match *)MatchesIterator_next(&matches)) != NULL)
            {
                starts[count++] = m->start;
            }

            assert(count == 1);
            assert(starts[0] == 1);

            MatchesIterator_drop(&matches);
            String_drop(&s);
        }

        // --- String_split_str ---
        {
            String s;
            String_from(&s, Str_from_cstr("one,two,three"));
            SplitIterator split = String_split_str(&s, Str_from_cstr(","));

            Str *part;
            const char *expected[] = {"one", "two", "three"};
            size_t i = 0;

            while ((part = (Str *)SplitIterator_next(&split)) != NULL)
            {
                assert(part->len == strlen(expected[i]));
                assert(memcmp(part->ptr, expected[i], part->len) == 0);
                i++;
            }

            assert(i == 3);

            SplitIterator_drop(&split);
            String_drop(&s);
        }
    }

    {
        Vec v;
        VecElementOps ops = {};
        Vec_new(&v, sizeof(size_t), &ops);

        for (size_t i = 1; i <= 5; i++)
        {
            Vec_push(&v, &i);
        }

        {
            VecIter it = Vec_iter(&v);

            assert(VecIter_len(&it) == 5);

            size_t i = 1;

            for (EACH_IN(elem, it))
            {
                assert(*(size_t *)elem == i);

                i++;
            }

            VecIter_drop(&it);

            assert(i == 6);
        }

        // SkipIter
        {
            VecIter it = Vec_iter(&v);
            AdapterIter _it = AdapterIter_new(
                VecIter_linked_iter(&it),
                (AdapterIterSpec[]){
                    AdapterIterSpec_skip(2),
                    AdapterIterSpec_none(),
                });

            assert(AdapterIter_len(&_it) == 3);

            size_t i = 3;

            for (EACH_IN(elem, _it))
            {
                assert(*(size_t *)elem == i);
                i++;
            }

            assert(i == 6);

            AdapterIter_drop(&_it);
        }

        // TakeIter
        {
            VecIter it = Vec_iter(&v);
            AdapterIter _it = AdapterIter_new(
                VecIter_linked_iter(&it),
                (AdapterIterSpec[]){
                    AdapterIterSpec_take(3),
                    AdapterIterSpec_none(),
                });

            assert(AdapterIter_len(&_it) == 3);

            size_t i = 1;

            for (EACH_IN(elem, _it))
            {
                assert(*(size_t *)elem == i);
                i++;
            }

            AdapterIter_drop(&_it);

            assert(i == 4);
        }

        // RevIter
        {
            VecIter it = Vec_iter(&v);
            AdapterIter _it = AdapterIter_new(
                VecIter_linked_iter(&it),
                (AdapterIterSpec[]){
                    AdapterIterSpec_rev(2),
                    AdapterIterSpec_none(),
                });

            assert(AdapterIter_len(&_it) == 5);

            size_t i = 5;

            for (EACH_IN(elem, _it))
            {
                assert(*(size_t *)elem == i);
                i--;
            }

            AdapterIter_drop(&_it);

            assert(i == 0);
        }

        // StepByIter
        {
            size_t new = 6;
            Vec_push(&v, &new);

            VecIter it = Vec_iter(&v);
            AdapterIter _it = AdapterIter_new(
                VecIter_linked_iter(&it),
                (AdapterIterSpec[]){
                    AdapterIterSpec_step_by(2),
                    AdapterIterSpec_none(),
                });

            assert(AdapterIter_len(&_it) == 3);

            size_t expected_step[] = {1, 3, 5};
            size_t i = 0;

            for (EACH_IN(elem, _it))
            {
                assert(*(size_t *)elem == expected_step[i]);
                i++;
            }

            AdapterIter_drop(&_it);

            assert(i == 3);
        }

        // StepByIter + Rev
        {
            VecIter it = Vec_iter(&v);
            AdapterIter _it = AdapterIter_new(
                VecIter_linked_iter(&it),
                (AdapterIterSpec[]){
                    AdapterIterSpec_step_by(2),
                    AdapterIterSpec_rev(),
                    AdapterIterSpec_none(),
                });

            assert(AdapterIter_len(&_it) == 3);

            size_t expected_step[] = {5, 3, 1};
            size_t i = 0;

            for (EACH_IN(elem, _it))
            {
                assert(*(size_t *)elem == expected_step[i]);
                i++;
            }

            AdapterIter_drop(&_it);

            assert(i == 3);
        }

        Vec_drop(&v);
    }

    {
        String str;
        String_from(&str, Str_from_cstr("a?b+c"));
        Regex regex = Regex_new(&str);
        String haystack;
        String_from(&haystack, Str_from_cstr("bbbbc"));

        Vec captures;
        assert(Regex_match(&regex, &haystack, &captures));

        String_drop(&str);
        String_drop(&haystack);
        Regex_drop(&regex);
    }

    {
        String str;
        String_from(&str, Str_from_cstr("a+?(a*)"));
        Regex regex = Regex_new(&str);
        String haystack;
        String_from(&haystack, Str_from_cstr("aaa"));

        Vec captures;
        assert(Regex_match(&regex, &haystack, &captures));

        assert(Vec_len(&captures) == 1);
        Capture *a = Vec_get_mut(&captures, 0);
        assert(a->start == 1 && a->end == 3);

        Vec_drop(&captures);
    }

    {
        String str;
        String_from(&str, Str_from_cstr("(a+)a*"));
        Regex regex = Regex_new(&str);
        String haystack;
        String_from(&haystack, Str_from_cstr("aaa"));

        Vec captures;
        assert(Regex_match(&regex, &haystack, &captures));

        assert(Vec_len(&captures) == 1);
        Capture *a = Vec_get_mut(&captures, 0);
        assert(a->start == 0 && a->end == 3);

        Vec_drop(&captures);
    }

    {
        String str;
        String_from(&str, Str_from_cstr("a*(b+)(c+)"));
        Regex regex = Regex_new(&str);
        String haystack;
        String_from(&haystack, Str_from_cstr("aaabc"));

        Vec captures;
        assert(Regex_match(&regex, &haystack, &captures));

        assert(Vec_len(&captures) == 2);
        Capture *a = Vec_get_mut(&captures, 0);
        assert(a->start == 3 && a->end == 4);
        a = Vec_get_mut(&captures, 1);
        assert(a->start == 4 && a->end == 5);

        Vec_drop(&captures);

        String_drop(&haystack);
        String_from(&haystack, Str_from_cstr("aaabbcc"));
        assert(Regex_match(&regex, &haystack, &captures));

        assert(Vec_len(&captures) == 2);
        a = Vec_get_mut(&captures, 0);
        assert(a->start == 3 && a->end == 5);
        a = Vec_get_mut(&captures, 1);
        assert(a->start == 5 && a->end == 7);

        Vec_drop(&captures);

        String_drop(&str);
        String_drop(&haystack);
        Regex_drop(&regex);
    }

    return 0;
}
