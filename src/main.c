#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#define ROUND_SIZE_UP_TO_ALIGN(size, align) (((size + align - 1) / align) * align)
#define ROUND_SIZE_UP_TO_MAX_ALIGN(size) ROUND_SIZE_UP_TO_ALIGN(size, _Alignof(max_align_t))

typedef void (*DropFn)(void *this);
typedef int_fast8_t (*CmpFn)(const void *a, const void *b);
typedef bool (*EqFn)(const void *a, const void *b);

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

typedef struct __BTreeMapNode
{
    struct __BTreeMapNode *parent;
    bool is_leaf;
    struct __BTreeMapNode *children[BTREEMAP_MAXIMUM_CHILD_COUNT];
    size_t key_count;
} _BTreeMapNode;

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

static size_t _BTreeMap_node_size(const BTreeMap *this)
{
    return ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(_BTreeMapNode)) + ROUND_SIZE_UP_TO_MAX_ALIGN(BTREEMAP_MAXIMUM_KEY_COUNT * this->key_props.size) + ROUND_SIZE_UP_TO_MAX_ALIGN(BTREEMAP_MAXIMUM_KEY_COUNT * this->value_props.size);
}

static void _BTreeMapNode_new(_BTreeMapNode *this, bool is_leaf)
{
    this->is_leaf = is_leaf;
    this->key_count = 0;
}

static void *_BTreeMapNode_key(_BTreeMapNode *this, const BTreeMap *map, size_t i)
{
    uint8_t *keys = (uint8_t *)(this) + ROUND_SIZE_UP_TO_MAX_ALIGN(sizeof(*this));
    return keys + (i * map->key_props.size);
}

static void *_BTreeMapNode_value(_BTreeMapNode *this, const BTreeMap *map, size_t i)
{
    uint8_t *values = (uint8_t *)(_BTreeMapNode_key(this, map, 0)) + ROUND_SIZE_UP_TO_MAX_ALIGN(BTREEMAP_MAXIMUM_KEY_COUNT * map->key_props.size);
    return values + (i * map->value_props.size);
}

static void _BTreeMapNode_shift_entries(_BTreeMapNode *this, const BTreeMap *map, size_t i, int8_t shift_by)
{
    const size_t entries_to_shift = this->key_count - i;

    if (entries_to_shift)
    {
        memmove(_BTreeMapNode_key(this, map, i + shift_by), _BTreeMapNode_key(this, map, i), entries_to_shift * map->key_props.size);
        memmove(_BTreeMapNode_value(this, map, i + shift_by), _BTreeMapNode_value(this, map, i), entries_to_shift * map->value_props.size);
    }
}

static void _BTreeMapNode_shift_children(_BTreeMapNode *this, const BTreeMap *map, size_t i, int8_t shift_by)
{
    const size_t children_to_shift = (this->key_count + 1) - i;

    if (children_to_shift)
    {
        memmove(&this->children[i + shift_by], &this->children[i], children_to_shift * sizeof(this->children[0]));
    }
}

static void _BTreeMapNode_remove_entry_at(_BTreeMapNode *this, const BTreeMap *map, size_t i)
{
    _BTreeMapNode_shift_entries(this, map, i, -1);
}

static void _BTreeMapNode_insert_entry_at(_BTreeMapNode *this, const BTreeMap *map, size_t i, const void *key, const void *value)
{
    _BTreeMapNode_shift_entries(this, map, i, 1);

    memcpy(_BTreeMapNode_key(this, map, i), key, map->key_props.size);
    memcpy(_BTreeMapNode_value(this, map, i), value, map->value_props.size);
}

static void _BTreeMapNode_insert_child_at(_BTreeMapNode *this, const BTreeMap *map, size_t i, void *child)
{
    _BTreeMapNode_shift_children(this, map, i, 1);

    this->children[i] = child;
}

static void _BTreeMapNode_remove_child_at(_BTreeMapNode *this, const BTreeMap *map, size_t i)
{
    _BTreeMapNode_shift_children(this, map, i, 1);
}

static void _BTreeMapNode_take_entries(_BTreeMapNode *this, _BTreeMapNode *other, const BTreeMap *map, size_t from_idx)
{
    const size_t entries_to_take = other->key_count - from_idx;

    memcpy(_BTreeMapNode_key(this, map, this->key_count), _BTreeMapNode_key(other, map, from_idx), entries_to_take * map->key_props.size);
    memcpy(_BTreeMapNode_value(this, map, this->key_count), _BTreeMapNode_value(other, map, from_idx), entries_to_take * map->value_props.size);
}

static void _BTreeMapNode_take_children(_BTreeMapNode *this, _BTreeMapNode *other, const BTreeMap *map, size_t from_idx)
{
    const size_t children_to_take = (other->key_count + 1) - from_idx;

    memcpy(_BTreeMapNode_key(this, map, this->key_count), _BTreeMapNode_key(other, map, from_idx), children_to_take * sizeof(this->children[0]));

    for (size_t i = 0; i < children_to_take; i++)
    {
        other->children[from_idx + i]->parent = this;
    }
}

static size_t _BTreeMapNode_child_idx(const _BTreeMapNode *this, _BTreeMapNode *child)
{
    size_t child_idx = SIZE_MAX;

    for (size_t i = 0; i < this->key_count + 1; i++)
    {
        if (this->children[i] == child)
        {
            child_idx = i;
        }
    }

    return child_idx;
}

static void _BTreeMapNode_drop(_BTreeMapNode *this, const BTreeMap *map)
{
    for (size_t i = 0; i < this->key_count; i++)
    {
        if (map->key_props.drop != NULL)
        {
            map->key_props.drop(_BTreeMapNode_key(this, map, i));
        }

        if (map->value_props.drop != NULL)
        {
            map->value_props.drop(_BTreeMapNode_value(this, map, i));
        }
    }

    if (!this->is_leaf)
    {
        for (size_t i = 0; i < this->key_count + 1; i++)
        {
            _BTreeMapNode_drop(this->children[i], map);
            free(this->children[i]);
        }
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
        struct
        {
            _BTreeMapNode *node;
            size_t key_idx;
        } found;

        struct
        {
            _BTreeMapNode *node;
            size_t child_idx;
        } go_down;
    };
} _GetResult;

static void _BTreeMap_get(BTreeMap *this, const void *key, _GetResult *result)
{
    _BTreeMapNode *current = this->root;

    while (true)
    {
        size_t i;
        for (i = 0; i < current->key_count && this->key_props.cmp(key, _BTreeMapNode_key(current, this, i)) > 0; i++)
            ;

        if (i < current->key_count && this->key_props.cmp(key, _BTreeMapNode_key(current, this, i)) == 0)
        {
            result->kind = GET_RESULT_KIND_FOUND;
            result->found.node = current;
            result->found.key_idx = i;
            break;
        }

        if (current->is_leaf)
        {
            result->kind = GET_RESULT_KIND_GO_DOWN;
            result->go_down.node = current;
            result->go_down.child_idx = i;
            break;
        }

        current = current->children[i];
    }
}

const void *BTreeMap_get(BTreeMap *this, const void *key)
{
    _GetResult result;
    _BTreeMap_get(this, key, &result);

    if (result.kind != GET_RESULT_KIND_FOUND)
    {
        return NULL;
    }

    return _BTreeMapNode_value(result.found.node, this, result.found.key_idx);
}

static void _BTreeMap_fix_overflow_up(BTreeMap *this, _BTreeMapNode *node)
{
    _BTreeMapNode *current = node;

    while (current->key_count == BTREEMAP_MAXIMUM_KEY_COUNT)
    {
        _BTreeMapNode *left = current;

        _BTreeMapNode *right = malloc(_BTreeMap_node_size(this));
        _BTreeMapNode_new(right, left->is_leaf);

        size_t mid = left->key_count / 2;
        _BTreeMapNode_take_entries(right, left, this, mid + 1);

        if (!left->is_leaf)
        {
            _BTreeMapNode_take_children(right, left, this, mid + 1);
        }

        right->key_count = left->key_count - mid + 1;

        void *separator_key = _BTreeMapNode_key(left, this, mid);
        void *separator_value = _BTreeMapNode_value(left, this, mid);
        _BTreeMapNode_remove_entry_at(left, this, mid);

        left->key_count = mid;

        if (current->parent == NULL)
        {
            _BTreeMapNode *new_root = malloc(_BTreeMap_node_size(this));
            _BTreeMapNode_new(new_root, false);

            _BTreeMapNode_insert_entry_at(new_root, this, 0, separator_key, separator_value);

            new_root->children[0] = left;
            left->parent = new_root;

            new_root->children[1] = right;
            right->parent = new_root;

            this->root = new_root;
            break;
        }
        else
        {
            _BTreeMapNode *parent = left->parent;
            size_t child_idx = _BTreeMapNode_child_idx(parent, left);

            _BTreeMapNode_insert_entry_at(parent, this, child_idx, separator_key, separator_value);

            parent->children[child_idx + 1] = right;
            right->parent = parent;

            current = parent;
        }
    }
}

void BTreeMap_insert(BTreeMap *this, const void *key, const void *value)
{
    _GetResult result;
    _BTreeMap_get(this, key, &result);

    if (result.kind == GET_RESULT_KIND_FOUND)
    {
        _BTreeMapNode *node = result.found.node;
        size_t idx = result.found.key_idx;

        if (this->value_props.drop != NULL)
        {
            this->value_props.drop(_BTreeMapNode_value(node, this, idx));
        }

        memcpy(_BTreeMapNode_value(node, this, idx), value, this->value_props.size);
    }
    else
    {
        _BTreeMapNode *node = result.go_down.node;
        size_t idx = result.go_down.child_idx;

        _BTreeMapNode_insert_entry_at(node, this, idx, key, value);
        node->key_count++;

        _BTreeMap_fix_overflow_up(this, node);
    }
}

static void _BTreeMapNode_borrow_from_left(_BTreeMapNode *parent, size_t child_idx, const BTreeMap *map)
{
    _BTreeMapNode *node = parent->children[child_idx];
    _BTreeMapNode *left = parent->children[child_idx - 1];

    // Insert separator at the front
    size_t separator_idx = child_idx - 1;
    void *separator_key = _BTreeMapNode_key(parent, map, separator_idx);
    void *separator_value = _BTreeMapNode_value(parent, map, separator_idx);
    _BTreeMapNode_insert_entry_at(node, map, 0, separator_key, separator_value);

    // Move last entry of left to separator slot
    void *last_key = _BTreeMapNode_key(left, map, left->key_count - 1);
    void *last_value = _BTreeMapNode_value(left, map, left->key_count - 1);
    _BTreeMapNode_remove_entry_at(left, map, left->key_count - 1);

    memmove(_BTreeMapNode_key(parent, map, separator_idx), last_key, map->key_props.size);
    memmove(_BTreeMapNode_value(parent, map, separator_idx), last_value, map->value_props.size);

    if (!node->is_leaf)
    {
        void *last_child = left->children[left->key_count];
        _BTreeMapNode_remove_child_at(left, map, left->key_count);

        _BTreeMapNode_insert_child_at(node, map, 0, last_child);
    }

    left->key_count--;
    node->key_count++;
}

static void _BTreeMapNode_borrow_from_right(_BTreeMapNode *parent, size_t child_idx, const BTreeMap *map)
{
    _BTreeMapNode *node = parent->children[child_idx];
    _BTreeMapNode *right = parent->children[child_idx + 1];

    // Insert separator at the back
    size_t separator_idx = child_idx;
    void *separator_key = _BTreeMapNode_key(parent, map, separator_idx);
    void *separator_value = _BTreeMapNode_value(parent, map, separator_idx);
    _BTreeMapNode_insert_entry_at(node, map, node->key_count, separator_key, separator_value);

    // Move first entry of right to separator slot
    void *last_key = _BTreeMapNode_key(right, map, 0);
    void *last_value = _BTreeMapNode_value(right, map, 0);
    _BTreeMapNode_remove_entry_at(right, map, 0);

    memmove(_BTreeMapNode_key(parent, map, separator_idx), last_key, map->key_props.size);
    memmove(_BTreeMapNode_value(parent, map, separator_idx), last_value, map->value_props.size);

    if (!node->is_leaf)
    {
        void *first_child = right->children[0];
        _BTreeMapNode_remove_child_at(right, map, 0);

        _BTreeMapNode_insert_child_at(node, map, node->key_count, first_child);
    }

    right->key_count--;
    node->key_count++;
}

static void _BTreeMapNode_merge_with_right(_BTreeMapNode *parent, size_t child_idx, const BTreeMap *map)
{
    _BTreeMapNode *node = parent->children[child_idx];
    _BTreeMapNode *right = parent->children[child_idx + 1];

    // Insert separator at the back
    size_t separator_idx = child_idx;
    void *separator_key = _BTreeMapNode_key(parent, map, separator_idx);
    void *separator_value = _BTreeMapNode_value(parent, map, separator_idx);
    _BTreeMapNode_insert_entry_at(node, map, node->key_count, separator_key, separator_value);
    node->key_count++;

    // Take all entries from sibling
    _BTreeMapNode_take_entries(node, right, map, 0);

    if (!node->is_leaf)
    {
        _BTreeMapNode_take_children(node, right, map, 0);
    }

    free(right);

    _BTreeMapNode_remove_entry_at(parent, map, separator_idx);
    _BTreeMapNode_remove_child_at(parent, map, separator_idx + 1);
}

static void _BTreeMap_fix_underflow_up(BTreeMap *this, _BTreeMapNode *node)
{
    _BTreeMapNode *current = node;

    while (current->key_count < BTREEMAP_MINIMUM_KEY_COUNT)
    {
        _BTreeMapNode *parent = current->parent;
        size_t child_idx = _BTreeMapNode_child_idx(parent, current);

        bool has_left_sibling = child_idx > 0;
        bool has_right_sibling = child_idx < parent->key_count + 1;

        if (has_left_sibling && parent->children[child_idx - 1]->key_count > BTREEMAP_MINIMUM_KEY_COUNT)
        {
            _BTreeMapNode_borrow_from_left(parent, child_idx, this);
            break;
        }
        else if (has_right_sibling && parent->children[child_idx + 1]->key_count > BTREEMAP_MINIMUM_KEY_COUNT)
        {
            _BTreeMapNode_borrow_from_right(parent, child_idx, this);
            break;
        }
        else
        {
            _BTreeMapNode_merge_with_right(parent, has_right_sibling ? child_idx : child_idx - 1, this);

            current = parent;
        }
    }
}

void BTreeMap_remove(BTreeMap *this, const void *key)
{
    _GetResult result;
    _BTreeMap_get(this, key, &result);

    if (result.kind != GET_RESULT_KIND_FOUND)
    {
        return;
    }

    _BTreeMapNode *node = result.found.node;
    size_t idx = result.found.key_idx;

    if (!node->is_leaf)
    {
        _BTreeMapNode *predecessor = node->children[idx];

        while (!predecessor->is_leaf)
        {
            predecessor = predecessor->children[predecessor->key_count];
        }

        _swap(_BTreeMapNode_key(node, this, idx), _BTreeMapNode_key(predecessor, this, predecessor->key_count - 1), this->key_props.size);
        _swap(_BTreeMapNode_value(node, this, idx), _BTreeMapNode_value(predecessor, this, predecessor->key_count - 1), this->value_props.size);

        node = predecessor;
        idx = predecessor->key_count - 1;
    }

    if (this->key_props.drop != NULL)
    {
        this->key_props.drop(_BTreeMapNode_key(node, this, idx));
    }

    if (this->value_props.drop != NULL)
    {
        this->value_props.drop(_BTreeMapNode_value(node, this, idx));
    }

    _BTreeMapNode_remove_entry_at(node, this, idx);

    _BTreeMap_fix_underflow_up(this, node);
}

void BTreeMap_new(BTreeMap *this, const BTreeMapKeyProps *key_props, const BTreeMapValueProps *value_props)
{
    this->key_props = *key_props;
    this->value_props = *value_props;

    this->length = 0;
    this->root = malloc(_BTreeMap_node_size(this));
    _BTreeMapNode_new(this->root, true);
}

void BTreeMap_drop(BTreeMap *this)
{
    _BTreeMapNode_drop(this->root, this);
    free(this->root);
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

#define MIN(a, b) (a < b ? a : b)

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
        this->element_props.drop(VecDeque_back(this));
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
            this->value_props.drop(_HashMapEntry_value(current_entry, this));
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
    this->value_props.drop(_HashMapEntry_value(_HashMap_entry_at(this, slot), this));

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
            this->value_props.drop(_HashMapEntry_value(entry, this));
        }
    }

    Hasher_drop(&this->hasher);

    free(this->entries);
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

void String_insert_str(String *this, size_t i, const char *str)
{
    // check if it is on byte boundary

    size_t length = strlen(str);

    if (length == 0)
    {
        return;
    }

    size_t spare_length = Vec_capacity(this->buffer) - Vec_len(this->buffer);

    if (length > spare_length)
    {
        size_t additional = length - spare_length;
        Vec_reserve(this->buffer, additional);
    }

    if (i != Vec_len(this->buffer))
    {
        memmove(Vec_get_mut(this->buffer, i + length), Vec_get(this->buffer, i), Vec_len(this->buffer) - i);
    }

    memcpy(Vec_get_mut(this->buffer, i), str, length);

    Vec_set_len(this->buffer, Vec_len(this->buffer) + length);
}

void String_from(String *this, const char *str)
{
    String_new(this);

    String_insert_str(this, 0, str);
}

void String_push_str(String *this, const char *str)
{
    String_insert_str(this, Vec_len(this->buffer), str);
}

void String_reserve(String *this, size_t additional)
{
    Vec_reserve(this->buffer, additional);
}

size_t String_len(const String *this)
{
    return Vec_len(this->buffer);
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

#include <stdint.h>

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

bool eq_u8(const void *a, const void *b)
{
    return *(const uint8_t *)a == *(const uint8_t *)b;
}

void hash_u8(const void *key, Hasher *hasher)
{
    Hasher_write(hasher, key, sizeof(uint8_t));
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
            .eq = eq_u8,
            .hash = hash_u8,
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
        String s1;
        String_new(&s1);

        assert(String_len(&s1) == 0);
        assert(String_capacity(&s1) == 0);

        String_drop(&s1);

        String s2;
        String_from(&s2, "hello");

        assert(String_len(&s2) == 5);
        assert(memcmp(Vec_get(s2.buffer, 0), "hello", 5) == 0);

        String_drop(&s2);

        String s3;
        String_new(&s3);

        String_push_str(&s3, "abc");
        assert(String_len(&s3) == 3);
        assert(memcmp(Vec_get(s3.buffer, 0), "abc", 3) == 0);

        String_push_str(&s3, "def");
        assert(String_len(&s3) == 6);
        assert(memcmp(Vec_get(s3.buffer, 0), "abcdef", 6) == 0);

        String_drop(&s3);

        String s4;
        String_from(&s4, "HelloWorld");

        String_insert_str(&s4, 5, " ");
        assert(String_len(&s4) == strlen("Hello World"));
        assert(memcmp(Vec_get(s4.buffer, 0), "Hello World", 11) == 0);

        String_insert_str(&s4, 0, "C");
        assert(memcmp(Vec_get(s4.buffer, 0), "CHello World", 12) == 0);

        String_insert_str(&s4, String_len(&s4), "!");
        assert(memcmp(Vec_get(s4.buffer, 0), "CHello World!", 13) == 0);

        String_drop(&s4);

        String s5;
        String_new(&s5);

        String_reserve(&s5, 10);
        assert(String_capacity(&s5) >= 10);
        assert(String_len(&s5) == 0);

        String_drop(&s5);
    }

    return 0;
}
