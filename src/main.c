#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// [Vec]

typedef struct
{
    void (*drop)(void *this);
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

size_t Vec_len(Vec *this)
{
    return this->length;
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

#include <stdbool.h>

// [BTreeMap]

#define BTREEMAP_T 3
#define BTREEMAP_MINIMUM_KEY_COUNT (BTREEMAP_T - 1)
#define BTREEMAP_MAXIMUM_KEY_COUNT (2 * BTREEMAP_T - 1)
#define BTREEMAP_MINIMUM_CHILD_COUNT (BTREEMAP_MINIMUM_KEY_COUNT + 1)
#define BTREEMAP_MAXIMUM_CHILD_COUNT (BTREEMAP_MAXIMUM_KEY_COUNT + 1)

typedef struct __BTreeMapNode
{
    bool is_leaf;
    size_t key_count;
    void *keys[BTREEMAP_MAXIMUM_KEY_COUNT];
    void *values[BTREEMAP_MAXIMUM_KEY_COUNT];
    struct __BTreeMapNode *children[BTREEMAP_MAXIMUM_CHILD_COUNT];
} _BTreeMapNode;

typedef struct
{
    void (*clone)(void *this, const void *src);
    void (*drop)(void *this);
    int32_t (*compare)(const void *a, const void *b);
} BTreeMapKeyOps;

typedef struct
{
    void (*clone)(void *this, const void *src);
    void (*drop)(void *this);
} BTreeMapValueOps;

typedef struct
{
    _BTreeMapNode *root;
    size_t key_size;
    BTreeMapKeyOps key_ops;
    size_t value_size;
    BTreeMapValueOps value_ops;
} BTreeMap;

static void _BTreeMap_clone_key(const BTreeMap *this, void *dst, const void *src)
{
    if (this->key_ops.clone)
    {
        this->key_ops.clone(dst, src);
    }
    else
    {
        // Assume element is POD and make a shallow copy
        memcpy(dst, src, this->key_size);
    }
}

static void _BTreeMap_drop_key(const BTreeMap *this, void *key)
{
    if (this->key_ops.drop)
    {
        this->key_ops.drop(key);
    }
    // If this->key_ops.drop == NULL, assume key is POD and doesn't need to be dropped
}

static int32_t _BTreeMap_compare_key(const BTreeMap *this, const void *a, const void *b)
{
    return this->key_ops.compare(a, b);
}

static void _BTreeMap_clone_value(const BTreeMap *this, void *dst, const void *src)
{
    if (this->value_ops.clone)
    {
        this->value_ops.clone(dst, src);
    }
    else
    {
        // Assume element is POD and make a shallow copy
        memcpy(dst, src, this->value_size);
    }
}

static void _BTreeMap_drop_value(const BTreeMap *this, void *value)
{
    if (this->value_ops.drop)
    {
        this->value_ops.drop(value);
    }
    // If this->value_ops.drop == NULL, assume value is POD and doesn't need to be dropped
}

static void _BTreeMapNode_new(_BTreeMapNode *this, bool is_leaf)
{
    this->is_leaf = is_leaf;
    this->key_count = 0;
}

static void _BTreeMapNode_drop(_BTreeMapNode *this, const BTreeMap *map)
{
    for (size_t i = 0; i < this->key_count; i++)
    {
        _BTreeMap_drop_key(map, this->keys[i]);
        free(this->keys[i]);

        _BTreeMap_drop_value(map, this->values[i]);
        free(this->values[i]);
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

static void _BTreeMapNode_get(_BTreeMapNode *this, const BTreeMap *map, const void *key, const void **value)
{
    size_t i;
    for (i = 0; i < this->key_count && _BTreeMap_compare_key(map, key, this->keys[i]) > 0; i++)
        ;

    if (i < this->key_count && _BTreeMap_compare_key(map, key, this->keys[i]) == 0)
    {
        *value = this->values[i];
    }
    else
    {
        if (this->is_leaf)
        {
            *value = NULL;
        }
        else
        {
            _BTreeMapNode_get(this->children[i], map, key, value);
        }
    }
}

static void _BTreeMapNode_split_child(_BTreeMapNode *this, size_t child_idx)
{
    _BTreeMapNode *full_child = this->children[child_idx];

    _BTreeMapNode *new_child = malloc(sizeof(_BTreeMapNode));
    _BTreeMapNode_new(new_child, full_child->is_leaf);

    // Move keys, values and children to new child

    new_child->key_count = BTREEMAP_MINIMUM_KEY_COUNT;

    memmove(new_child->keys, full_child->keys + BTREEMAP_T, sizeof(new_child->keys[0]) * BTREEMAP_MINIMUM_KEY_COUNT);
    memmove(new_child->values, full_child->values + BTREEMAP_T, sizeof(new_child->values[0]) * BTREEMAP_MINIMUM_KEY_COUNT);

    if (!full_child->is_leaf)
    {
        memmove(new_child->children, full_child->children + BTREEMAP_T, sizeof(new_child->children[0]) * BTREEMAP_MINIMUM_CHILD_COUNT);
    }

    full_child->key_count = BTREEMAP_T;

    // Shift parent's keys/values and insert median

    void *median_key = full_child->keys[BTREEMAP_T - 1];
    void *median_value = full_child->values[BTREEMAP_T - 1];

    full_child->key_count--;

    const size_t elements_to_move = this->key_count - child_idx;

    this->key_count++;

    memmove(this->keys + child_idx + 1, this->keys + child_idx, elements_to_move * sizeof(this->keys[0]));
    memmove(this->values + child_idx + 1, this->values + child_idx, elements_to_move * sizeof(this->values[0]));

    this->keys[child_idx] = median_key;
    this->values[child_idx] = median_value;

    memmove(this->children + child_idx + 2, this->children + child_idx + 1, elements_to_move * sizeof(this->children[0]));

    this->children[child_idx + 1] = new_child;
}

static void _BTreeMapNode_insert(_BTreeMapNode *this, BTreeMap *map, const void *key, const void *value)
{
    if (this->is_leaf)
    {
        size_t i;
        for (i = 0; i < this->key_count && _BTreeMap_compare_key(map, key, this->keys[i]) > 0; i++)
            ;

        if (i < this->key_count && _BTreeMap_compare_key(map, key, this->keys[i]) == 0)
        {
            _BTreeMap_drop_value(map, this->values[i]);
            _BTreeMap_clone_value(map, this->values[i], value);
            return;
        }

        const size_t elements_to_move = this->key_count - i;

        this->key_count++;

        memmove(this->keys + i + 1, this->keys + i, elements_to_move * sizeof(this->keys[0]));
        memmove(this->values + i + 1, this->values + i, elements_to_move * sizeof(this->values[0]));

        this->keys[i] = malloc(map->key_size);
        _BTreeMap_clone_key(map, this->keys[i], key);
        this->values[i] = malloc(map->value_size);
        _BTreeMap_clone_value(map, this->values[i], value);
    }
    else
    {
        size_t i;
        for (i = 0; i < this->key_count && _BTreeMap_compare_key(map, key, this->keys[i]) > 0; i++)
            ;

        if (i < this->key_count && _BTreeMap_compare_key(map, key, this->keys[i]) == 0)
        {
            _BTreeMap_drop_value(map, this->values[i]);
            _BTreeMap_clone_value(map, this->values[i], value);
            return;
        }

        if (this->children[i]->key_count == BTREEMAP_MAXIMUM_KEY_COUNT)
        {
            _BTreeMapNode_split_child(this, i);

            // Compare with the median brought up from the child
            int32_t result = _BTreeMap_compare_key(map, key, this->keys[i]);
            if (result == 0)
            {
                _BTreeMap_drop_value(map, this->values[i]);
                _BTreeMap_clone_value(map, this->values[i], value);
                return;
            }

            i = result > 0 ? i + 1 : i;
        }

        _BTreeMapNode_insert(this->children[i], map, key, value);
    }
}

static void _BTreeMapNode_borrow_from_right(_BTreeMapNode *this, size_t child_idx)
{
    _BTreeMapNode *child = this->children[child_idx];
    _BTreeMapNode *sibling = this->children[child_idx + 1];

    child->keys[child->key_count] = this->keys[child_idx];
    child->values[child->key_count] = this->values[child_idx];

    if (!child->is_leaf)
    {
        child->children[child->key_count + 1] = sibling->children[0];
    }

    child->key_count++;

    this->keys[child_idx] = sibling->keys[0];
    this->values[child_idx] = sibling->values[0];

    const size_t elements_to_move = sibling->key_count - 1;
    memmove(sibling->keys, sibling->keys + 1, sizeof(sibling->keys[0]) * elements_to_move);
    memmove(sibling->values, sibling->values + 1, sizeof(sibling->values[0]) * elements_to_move);

    if (!sibling->is_leaf)
    {
        memmove(sibling->children, sibling->children + 1, sizeof(sibling->children[0]) * elements_to_move + 1);
    }

    sibling->key_count--;
}

static void _BTreeMapNode_borrow_from_left(_BTreeMapNode *this, size_t child_idx)
{
    _BTreeMapNode *child = this->children[child_idx];
    _BTreeMapNode *sibling = this->children[child_idx - 1];

    const size_t elements_to_move = child->key_count;
    memmove(child->keys + 1, child->keys, sizeof(child->keys[0]) * elements_to_move);
    memmove(child->values + 1, child->values, sizeof(child->values[0]) * elements_to_move);

    if (!child->is_leaf)
    {
        memmove(child->children + 1, child->children, sizeof(child->children[0]) * elements_to_move + 1);
    }

    child->keys[0] = this->keys[child_idx - 1];
    child->values[0] = this->values[child_idx - 1];

    if (!child->is_leaf)
    {
        child->children[0] = sibling->children[sibling->key_count];
    }

    child->key_count++;

    this->keys[child_idx - 1] = sibling->keys[sibling->key_count - 1];
    this->values[child_idx - 1] = sibling->values[sibling->key_count - 1];

    sibling->key_count--;
}

static void _BTreeMapNode_merge_with_right(_BTreeMapNode *this, size_t child_idx)
{
    _BTreeMapNode *child = this->children[child_idx];
    _BTreeMapNode *sibling = this->children[child_idx + 1];

    child->keys[child->key_count] = this->keys[child_idx];
    child->values[child->key_count] = this->values[child_idx];

    size_t elements_to_move = sibling->key_count;
    memmove(child->keys + BTREEMAP_MINIMUM_KEY_COUNT + 1, sibling->keys, elements_to_move * sizeof(sibling->keys[0]));
    memmove(child->values + BTREEMAP_MINIMUM_KEY_COUNT + 1, sibling->values, elements_to_move * sizeof(sibling->values[0]));

    if (!child->is_leaf)
    {
        memmove(child->children + BTREEMAP_MINIMUM_KEY_COUNT + 1, sibling->children, (elements_to_move + 1) * sizeof(sibling->values[0]));
    }

    child->key_count += sibling->key_count + 1;

    free(sibling);

    elements_to_move = this->key_count - child_idx - 1;
    memmove(this->keys + child_idx, this->keys + child_idx + 1, elements_to_move * sizeof(this->keys[0]));
    memmove(this->values + child_idx, this->values + child_idx + 1, elements_to_move * sizeof(this->values[0]));

    memmove(this->children + child_idx + 1, this->children + child_idx + 2, (elements_to_move + 1) * sizeof(this->children[0]));

    this->key_count--;
}

static void _BTreeMapNode_fill_child(_BTreeMapNode *this, size_t *child_idx)
{
    bool is_leftmost = *child_idx == 0;
    bool is_rightmost = *child_idx == this->key_count;

    if (is_leftmost)
    {
        if (this->children[*child_idx + 1]->key_count > BTREEMAP_MINIMUM_KEY_COUNT)
        {
            _BTreeMapNode_borrow_from_right(this, *child_idx);
        }
        else
        {
            _BTreeMapNode_merge_with_right(this, *child_idx);
        }
    }
    else if (is_rightmost)
    {
        if (this->children[*child_idx - 1]->key_count > BTREEMAP_MINIMUM_KEY_COUNT)
        {
            _BTreeMapNode_borrow_from_left(this, *child_idx);
        }
        else
        {
            _BTreeMapNode_merge_with_right(this, *child_idx - 1);

            *child_idx = *child_idx - 1;
        }
    }
    else
    {
        if (this->children[*child_idx + 1]->key_count > BTREEMAP_MINIMUM_KEY_COUNT)
        {
            _BTreeMapNode_borrow_from_right(this, *child_idx);
        }
        else if (this->children[*child_idx - 1]->key_count > BTREEMAP_MINIMUM_KEY_COUNT)
        {
            _BTreeMapNode_borrow_from_left(this, *child_idx);
        }
        else
        {
            _BTreeMapNode_merge_with_right(this, *child_idx);
        }
    }
}

static void _BTreeMapNode_get_predecessor(_BTreeMapNode *this, size_t child_idx, void **key, void **value)
{
    _BTreeMapNode *current = this->children[child_idx];

    while (!current->is_leaf)
    {
        current = current->children[current->key_count];
    }

    *key = current->keys[current->key_count - 1];
    *value = current->values[current->key_count - 1];
}

static void _BTreeMapNode_get_successor(_BTreeMapNode *this, size_t child_idx, void **key, void **value)
{
    _BTreeMapNode *current = this->children[child_idx];

    while (!current->is_leaf)
    {
        current = current->children[0];
    }

    *key = current->keys[0];
    *value = current->values[0];
}

static void _BTreeMapNode_remove(_BTreeMapNode *this, const BTreeMap *map, const void *key)
{
    size_t i;
    for (i = 0; i < this->key_count && _BTreeMap_compare_key(map, key, this->keys[i]) > 0; i++)
        ;

    if (i < this->key_count && _BTreeMap_compare_key(map, key, this->keys[i]) == 0)
    {
        if (this->is_leaf)
        {
            // Key found in leaf

            _BTreeMap_drop_key(map, this->keys[i]);
            free(this->keys[i]);

            _BTreeMap_drop_value(map, this->values[i]);
            free(this->values[i]);

            const size_t elements_to_move = this->key_count - i - 1;
            memmove(this->keys + i, this->keys + i + 1, elements_to_move * sizeof(this->keys[0]));
            memmove(this->values + i, this->values + i + 1, elements_to_move * sizeof(this->values[0]));

            this->key_count--;
        }
        else
        {
            // Key found in internal node

            if (this->children[i]->key_count > BTREEMAP_MINIMUM_KEY_COUNT)
            {
                void *predecessor_key;
                void *predecessor_value;
                _BTreeMapNode_get_predecessor(this, i, &predecessor_key, &predecessor_value);

                _BTreeMap_drop_key(map, this->keys[i]);
                _BTreeMap_clone_key(map, this->keys[i], predecessor_key);

                _BTreeMap_drop_value(map, this->values[i]);
                _BTreeMap_clone_value(map, this->values[i], predecessor_value);

                _BTreeMapNode_remove(this->children[i], map, predecessor_key);
            }
            else if (this->children[i + 1]->key_count > BTREEMAP_MINIMUM_KEY_COUNT)
            {
                void *successor_key;
                void *successor_value;
                _BTreeMapNode_get_successor(this, i + 1, &successor_key, &successor_value);

                _BTreeMap_drop_key(map, this->keys[i]);
                _BTreeMap_clone_key(map, this->keys[i], successor_key);

                _BTreeMap_drop_value(map, this->values[i]);
                _BTreeMap_clone_value(map, this->values[i], successor_value);

                _BTreeMapNode_remove(this->children[i + 1], map, successor_key);
            }
            else
            {
                _BTreeMapNode_merge_with_right(this, i);
                _BTreeMapNode_remove(this->children[i], map, key);
            }
        }
    }
    else
    {
        if (this->is_leaf)
        {
            return;
        }

        if (this->children[i]->key_count == BTREEMAP_MINIMUM_KEY_COUNT)
        {
            _BTreeMapNode_fill_child(this, &i);
        }

        _BTreeMapNode_remove(this->children[i], map, key);
    }
}

void BTreeMap_new(BTreeMap *this, size_t key_size, BTreeMapKeyOps *key_ops, size_t value_size, const BTreeMapValueOps *value_ops)
{
    this->key_size = key_size;
    this->key_ops = *key_ops;
    this->value_size = value_size;
    this->value_ops = *value_ops;

    this->root = malloc(sizeof(_BTreeMapNode));
    _BTreeMapNode_new(this->root, true);
}

void BTreeMap_get(BTreeMap *this, const void *key, const void **value)
{
    _BTreeMapNode_get(this->root, this, key, value);
}

void BTreeMap_insert(BTreeMap *this, const void *key, const void *value)
{
    if (this->root->key_count == BTREEMAP_MAXIMUM_KEY_COUNT)
    {
        _BTreeMapNode *new_root = malloc(sizeof(_BTreeMapNode));
        _BTreeMapNode_new(new_root, false);

        new_root->children[0] = this->root;
        _BTreeMapNode_split_child(new_root, 0);

        this->root = new_root;
    }

    _BTreeMapNode_insert(this->root, this, key, value);
}

void BTreeMap_remove(BTreeMap *this, const void *key)
{
    _BTreeMapNode_remove(this->root, this, key);

    if (this->root->key_count == 0)
    {
        if (!this->root->is_leaf)
        {
            _BTreeMapNode *old_root = this->root;
            this->root = old_root->children[0];
            free(old_root);
        }
    }
}

void BTreeMap_drop(BTreeMap *this)
{
    _BTreeMapNode_drop(this->root, this);
    free(this->root);
}

// [LinkedList]

#include <stddef.h>

#define ROUND_SIZE_UP_TO_ALIGN(size, align) (((size + align - 1) / align) * align)
#define ROUND_SIZE_UP_TO_MAX_ALIGN(size) ROUND_SIZE_UP_TO_ALIGN(size, _Alignof(max_align_t))

typedef struct _LinkedListNode
{
    struct _LinkedListNode *next;
    struct _LinkedListNode *previous;
} LinkedListNode;

typedef struct
{
    size_t element_size;
    void (*drop)(void *this);
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
    void (*drop)(void *this);
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

void drop_pod(void *element)
{
    (void)element;
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
        BTreeMapKeyOps key_ops = {};
        key_ops.compare = compare_u8;
        BTreeMapValueOps value_ops = {};
        BTreeMap_new(&u8u8map, sizeof(uint8_t), &key_ops, sizeof(uint8_t), &value_ops);

        for (uint8_t i = 1; i <= 3; i++)
        {
            BTreeMap_insert(&u8u8map, &i, &i);
        }

        for (uint8_t i = 1; i <= 3; i++)
        {
            const void *value = NULL;
            BTreeMap_get(&u8u8map, &i, &value);

            assert(value != NULL);
            assert(*(uint8_t *)value == i);
        }

        uint8_t key = 2, new_value = 42;
        BTreeMap_insert(&u8u8map, &key, &new_value);

        const void *value = NULL;
        BTreeMap_get(&u8u8map, &key, &value);

        assert(value != NULL);
        assert(*(uint8_t *)value == 42);

        // Remove key = 1 (leaf case)
        key = 1;
        BTreeMap_remove(&u8u8map, &key);
        BTreeMap_get(&u8u8map, &key, &value);
        assert(value == NULL);

        // Remove key = 2 (internal node candidate)
        key = 2;
        BTreeMap_remove(&u8u8map, &key);
        BTreeMap_get(&u8u8map, &key, &value);
        assert(value == NULL);

        // Try removing a non-existent key
        key = 99;
        BTreeMap_remove(&u8u8map, &key);
        BTreeMap_get(&u8u8map, &key, &value);
        assert(value == NULL);

        BTreeMap_drop(&u8u8map);
    }

    {
        LinkedListElementProps element_props;
        element_props.element_size = sizeof(uint8_t);
        element_props.drop = drop_pod;

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
            .drop = drop_pod,
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

    return 0;
}
