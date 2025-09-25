#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    void (*clone)(void *this, const void *src);
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

static void _Vec_clone_element(Vec *this, void *dst, const void *src)
{
    if (this->element_ops.clone)
    {
        this->element_ops.clone(dst, src);
    }
    else
    {
        // Assume element is POD and make a shallow copy
        memcpy(dst, src, this->element_size);
    }
}

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
        this->element_ops.clone = NULL;
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

void Vec_set(Vec *this, size_t i, const void *value)
{
    void *element = Vec_get_mut(this, i);

    _Vec_drop_element(this, element);
    _Vec_clone_element(this, element, value);
}

void Vec_push(Vec *this, const void *value)
{
    if (this->length == this->capacity)
    {
        size_t new_capacity = this->capacity ? this->capacity * 2 : 10;

        this->data = realloc(this->data, new_capacity * this->element_size);
        this->capacity = new_capacity;
    }

    _Vec_clone_element(this, Vec_get_mut(this, this->length), value);

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

        _Vec_clone_element(this, new_element_address, value);

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

#include <stdint.h>
#include <assert.h>

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
            uint8_t element = *(uint8_t *)Vec_get(&u8vec, i);

            element = element * 2;

            Vec_set(&u8vec, i, &element);

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

    return 0;
}