// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cassert>
#include <cstring> // for memcpy
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include "QirContext.hpp"
#include "QirTypes.hpp"
#include "QirRuntime.hpp"

// Exposed to tests only:
QirTupleHeader* FlattenControlArrays(QirTupleHeader* tuple, int depth);

using namespace Microsoft::Quantum;

/*==============================================================================
    Implementation of __quantum__rt__tuple_* and __quantum__rt__callable_*
==============================================================================*/
extern "C"
{
    PTuple __quantum__rt__tuple_create(int64_t size) // TODO: Use unsigned integer type for param (breaking change).
    {
        assert((uint64_t)size < std::numeric_limits<QirTupleHeader::TBufSize>::max());
        // Using `<` rather than `<=` to calm down the compiler on 64-bit arch.
        return QirTupleHeader::Create(static_cast<QirTupleHeader::TBufSize>(size))->AsTuple();
    }

    void __quantum__rt__tuple_update_reference_count(PTuple tuple, int32_t increment)
    {
        if (tuple == nullptr || increment == 0)
        {
            return;
        }

        QirTupleHeader* th = QirTupleHeader::GetHeader(tuple);
        if (increment > 0)
        {
            for (int i = 0; i < increment; i++)
            {
                (void)th->AddRef();
            }
        }
        else
        {
            for (int i = increment; i < 0; i++)
            {
                (void)th->Release();
            }
        }
    }

    void __quantum__rt__tuple_update_alias_count(PTuple tuple, int32_t increment)
    {
        if (tuple == nullptr || increment == 0)
        {
            return;
        }
        QirTupleHeader* th = QirTupleHeader::GetHeader(tuple);
        th->aliasCount += increment;

        if (th->aliasCount < 0)
        {
            __quantum__rt__fail(__quantum__rt__string_create("Alias count cannot be negative"));
        }
    }

    PTuple __quantum__rt__tuple_copy(PTuple tuple, bool forceNewInstance)
    {
        if (tuple == nullptr)
        {
            return nullptr;
        }

        QirTupleHeader* th = QirTupleHeader::GetHeader(tuple);
        if (forceNewInstance || th->aliasCount > 0)
        {
            return QirTupleHeader::CreateWithCopiedData(th)->AsTuple();
        }

        th->AddRef();
        return tuple;
    }

    void __quantum__rt__callable_update_reference_count(QirCallable* callable, int32_t increment)
    {
        if (callable == nullptr || increment == 0)
        {
            return;
        }
        else if (increment > 0)
        {
            for (int i = 0; i < increment; i++)
            {
                (void)callable->AddRef();
            }
        }
        else
        {
            for (int i = increment; i < 0; i++)
            {
                if (0 == callable->Release())
                {
                    assert(-1 == i && "Attempting to decrement reference count below zero!");
                    break;
                }
            }
        }
    }

    void __quantum__rt__callable_update_alias_count(QirCallable* callable, int32_t increment)
    {
        if (callable == nullptr || increment == 0)
        {
            return;
        }
        callable->UpdateAliasCount(increment);
    }

    QirCallable* __quantum__rt__callable_create(t_CallableEntry* entries, t_CaptureCallback* captureCallbacks,
                                                PTuple capture)
    {
        assert(entries != nullptr);
        return new QirCallable(entries, captureCallbacks, capture);
    }

    void __quantum__rt__callable_invoke(QirCallable* callable, PTuple args, PTuple result)
    {
        assert(callable != nullptr);
        callable->Invoke(args, result);
    }

    QirCallable* __quantum__rt__callable_copy(QirCallable* other, bool forceNewInstance)
    {
        if (other == nullptr)
        {
            return nullptr;
        }
        if (forceNewInstance)
        {
            return new QirCallable(*other);
        }
        return other->CloneIfShared();
    }

    void __quantum__rt__callable_make_adjoint(QirCallable* callable)
    {
        assert(callable != nullptr);
        callable->ApplyFunctor(QirCallable::Adjoint);
    }

    void __quantum__rt__callable_make_controlled(QirCallable* callable)
    {
        assert(callable != nullptr);
        callable->ApplyFunctor(QirCallable::Controlled);
    }

    void __quantum__rt__capture_update_reference_count(QirCallable* callable, int32_t parameter)
    {
        callable->InvokeCaptureCallback(0, parameter);
    }

    void __quantum__rt__capture_update_alias_count(QirCallable* callable, int32_t parameter)
    {
        callable->InvokeCaptureCallback(1, parameter);
    }
}

/*==============================================================================
    Implementation of QirTupleHeader
==============================================================================*/
int QirTupleHeader::AddRef()
{
    if (GlobalContext() != nullptr)
    {
        GlobalContext()->OnAddRef(this);
    }

    assert(this->refCount > 0);
    return ++this->refCount;
}

int QirTupleHeader::Release()
{
    if (GlobalContext() != nullptr)
    {
        GlobalContext()->OnRelease(this);
    }


    assert(this->refCount > 0); // doesn't guarantee we catch double releases but better than nothing
    int retVal = --this->refCount;
    if (this->refCount == 0)
    {
        PTuplePointedType* buffer = reinterpret_cast<PTuplePointedType*>(this);
        delete[] buffer;
    }
    return retVal;
}

QirTupleHeader* QirTupleHeader::Create(TBufSize size)
{
    PTuplePointedType* buffer = new PTuplePointedType[sizeof(QirTupleHeader) + size];

    if (GlobalContext() != nullptr)
    {
        GlobalContext()->OnAllocate(buffer);
    }

    // at the beginning of the buffer place QirTupleHeader, leave the buffer uninitialized
    QirTupleHeader* th = reinterpret_cast<QirTupleHeader*>(buffer);
    th->refCount       = 1;
    th->aliasCount     = 0;
    th->tupleSize      = size;

    return th;
}

QirTupleHeader* QirTupleHeader::CreateWithCopiedData(QirTupleHeader* other)
{
    const TBufSize size       = other->tupleSize;
    PTuplePointedType* buffer = new PTuplePointedType[sizeof(QirTupleHeader) + size];

    if (GlobalContext() != nullptr)
    {
        GlobalContext()->OnAllocate(buffer);
    }

    // at the beginning of the buffer place QirTupleHeader
    QirTupleHeader* th = reinterpret_cast<QirTupleHeader*>(buffer);
    th->refCount       = 1;
    th->aliasCount     = 0;
    th->tupleSize      = size;

    // copy the contents of the other tuple
    memcpy(th->AsTuple(), other->AsTuple(), size);

    return th;
}

/*==============================================================================
    Implementation of QirCallable
==============================================================================*/
QirCallable::~QirCallable()
{
    assert(refCount == 0);
}

QirCallable::QirCallable(const t_CallableEntry* ftEntries, const t_CaptureCallback* callbacks, PTuple capt)
    : refCount(1)
    , capture(capt)
    , appliedFunctor(0)
    , controlledDepth(0)
{
    memcpy(this->functionTable, ftEntries, sizeof(this->functionTable));
    assert(this->functionTable[0] != nullptr); // base must be always defined

    if (callbacks != nullptr)
    {
        memcpy(this->captureCallbacks, callbacks, sizeof(this->captureCallbacks));
    }

    if (GlobalContext() != nullptr)
    {
        GlobalContext()->OnAllocate(this);
    }
}

QirCallable::QirCallable(const QirCallable& other)
    : refCount(1)
    , capture(other.capture)
    , appliedFunctor(other.appliedFunctor)
    , controlledDepth(other.controlledDepth)
{
    memcpy(this->functionTable, other.functionTable, sizeof(this->functionTable));
    memcpy(this->captureCallbacks, other.captureCallbacks, sizeof(this->captureCallbacks));

    if (GlobalContext() != nullptr)
    {
        GlobalContext()->OnAllocate(this);
    }
}

QirCallable* QirCallable::CloneIfShared()
{
    if (this->aliasCount == 0)
    {
        this->AddRef();
        return this;
    }
    return new QirCallable(*this);
}

int QirCallable::AddRef()
{
    if (GlobalContext() != nullptr)
    {
        GlobalContext()->OnAddRef(this);
    }

    int rc = ++this->refCount;
    assert(rc != 1); // not allowed to resurrect!
    return rc;
}

int QirCallable::Release()
{
    if (GlobalContext() != nullptr)
    {
        GlobalContext()->OnRelease(this);
    }
    assert(this->refCount > 0);

    int rc = --this->refCount;
    if (rc == 0)
    {
        delete this;
    }
    return rc;
}

void QirCallable::UpdateAliasCount(int increment)
{
    this->aliasCount += increment;
    if (this->aliasCount < 0)
    {
        __quantum__rt__fail(__quantum__rt__string_create("Alias count cannot be negative"));
    }
}

// The function _assumes_ a particular structure of the passed in tuple (see
// https://github.com/microsoft/qsharp-language/blob/main/Specifications/QIR/Callables.md) and recurses into it upto
// the given depth to create a new tuple with a combined array of controls.
//
// For example, `src` tuple might point to a tuple with the following structure (depth = 2):
// { %Array*, { %Array*, { i64, %Qubit* }* }* }
// or it might point to a tuple where the inner type isn't a tuple but a built-in type (depth = 2):
// { %Array*, { %Array*, %Qubit* }* }
// The function will create a new tuple, where the array contains elements of all nested arrays, respectively for the
// two cases will get:
// { %Array*, { i64, %Qubit* }* }
// { %Array*, %Qubit* }
// The caller is responsible for releasing both the returned tuple and the array it contains.
// The order of the elements in the array is unspecified.
QirTupleHeader* FlattenControlArrays(QirTupleHeader* tuple, int depth)
{
    assert(depth > 1); // no need to unpack at depth 1, and should avoid allocating unnecessary tuples

    const QirArray::TItemSize qubitSize =
        sizeof(/*Qubit*/ void*); // Compiler complains for `sizeof(Qubit)`:
                                 // warning: suspicious usage of 'sizeof(A*)'; pointer to aggregate
                                 // [bugprone-sizeof-expression]. To be fixed when the `Qubit` is made fixed-size type.

    TupleWithControls* outer = TupleWithControls::FromTupleHeader(tuple);

    // Discover, how many controls there are in total so can allocate a correctly sized array for them.
    QirArray::TItemCount cControls = 0;
    TupleWithControls* current     = outer;
    for (int i = 0; i < depth; i++)
    {
        assert(i == depth - 1 || current->GetHeader()->tupleSize == sizeof(TupleWithControls));
        QirArray* controls = current->controls;
        assert(controls->itemSizeInBytes == qubitSize);
        cControls += controls->count;
        current = current->innerTuple;
    }

    // Copy the controls into the new array. This array doesn't own the qubits so must use the generic constructor.
    QirArray* combinedControls          = new QirArray(cControls, qubitSize);
    char* dst                           = combinedControls->buffer;
    [[maybe_unused]] const char* dstEnd = dst + qubitSize * cControls;
    current                             = outer;
    QirTupleHeader* last                = nullptr;
    for (int i = 0; i < depth; i++)
    {
        if (i == depth - 1)
        {
            last = current->GetHeader();
        }

        QirArray* controls = current->controls;

        const QirArray::TBufSize blockSize = qubitSize * controls->count;
        // Make sure we don't overflow `TBufSize` on 32-bit arch:
        assert((blockSize >= qubitSize) && (blockSize >= controls->count));

        assert(dst + blockSize <= dstEnd);
        memcpy(dst, controls->buffer, blockSize);
        dst += blockSize;
        // in the last iteration the innerTuple isn't valid, but we are not going to use it
        current = current->innerTuple;
    }

    // Create the new tuple with the flattened controls array and args from the `last` tuple.
    QirTupleHeader* flatTuple = QirTupleHeader::CreateWithCopiedData(last);
    QirArray** arr            = reinterpret_cast<QirArray**>(flatTuple->AsTuple());
    *arr                      = combinedControls;

    return flatTuple;
}

void QirCallable::Invoke(PTuple args, PTuple result)
{
    assert(this->appliedFunctor < QirCallable::TableSize);
    if (this->controlledDepth < 2)
    {
        // For uncontrolled or singly-controlled callables, the `args` tuple is "flat" and can be passed directly to the
        // implementing function.
        this->functionTable[this->appliedFunctor](capture, args, result);
    }
    else
    {
        // Must unpack the `args` tuple into a new tuple with flattened controls.
        QirTupleHeader* flat = FlattenControlArrays(QirTupleHeader::GetHeader(args), this->controlledDepth);
        this->functionTable[this->appliedFunctor](capture, flat->AsTuple(), result);

        QirArray* controls = *reinterpret_cast<QirArray**>(flat->AsTuple());
        __quantum__rt__array_update_reference_count(controls, -1);
        flat->Release();
    }
}

void QirCallable::Invoke()
{
    assert((this->appliedFunctor & QirCallable::Controlled) == 0 && "Cannot invoke controlled callable without args");
    PTuple args = __quantum__rt__tuple_create(0);
    this->Invoke(args, nullptr);
    __quantum__rt__tuple_update_reference_count(args, -1);
}

// A + A = I; A + C = C + A = CA; C + C = C; CA + A = C; CA + C = CA
void QirCallable::ApplyFunctor(int functor)
{
    assert(functor == QirCallable::Adjoint || functor == QirCallable::Controlled);

    if (functor == QirCallable::Adjoint)
    {
        this->appliedFunctor ^= QirCallable::Adjoint;
        if (this->functionTable[this->appliedFunctor] == nullptr)
        {
            this->appliedFunctor ^= QirCallable::Adjoint;
            __quantum__rt__fail(__quantum__rt__string_create("The callable doesn't provide adjoint operation"));
        }
    }
    else if (functor == QirCallable::Controlled)
    {
        this->appliedFunctor |= QirCallable::Controlled;
        if (this->functionTable[this->appliedFunctor] == nullptr)
        {
            if (this->controlledDepth == 0)
            {
                this->appliedFunctor ^= QirCallable::Controlled;
            }
            __quantum__rt__fail(__quantum__rt__string_create("The callable doesn't provide controlled operation"));
        }
        this->controlledDepth++;
    }
}

void QirCallable::InvokeCaptureCallback(int32_t index, int32_t parameter)
{
    assert(index >= 0 && index < QirCallable::CaptureCallbacksTableSize && "Capture callback index out of range");

    if (this->captureCallbacks[index] != nullptr)
    {
        this->captureCallbacks[index](this->capture, parameter);
    }
}
