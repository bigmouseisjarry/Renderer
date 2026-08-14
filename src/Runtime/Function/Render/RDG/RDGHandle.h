#pragma once

#include "Core/DependencyGraph/DependencyGraph.h"

#include <cstdint>


class RDGResoruceHandle
{
public:
    RDGResoruceHandle(uint64_t id) : id(id) {}

    bool operator< (const RDGResoruceHandle& other) const noexcept {
        return id < other.id;
    }

    bool operator== (const RDGResoruceHandle& other) const noexcept {
        return (id == other.id);
    }

    bool operator!= (const RDGResoruceHandle& other) const noexcept {
        return !operator==(other);
    }

    inline uint64_t ID() { return id; }

protected:
    uint64_t id = UINT64_MAX;
};

class RDGPassHandle : public RDGResoruceHandle
{
public:
    RDGPassHandle(uint64_t id = UINT64_MAX) : RDGResoruceHandle(id) {};
};

class RDGRenderPassHandle : public RDGPassHandle
{
public:
    RDGRenderPassHandle(uint64_t id = UINT64_MAX) : RDGPassHandle(id) {};
};

class RDGComputePassHandle : public RDGPassHandle
{
public:
    RDGComputePassHandle(uint64_t id = UINT64_MAX) : RDGPassHandle(id) {};
};

class RDGRayTracingPassHandle : public RDGPassHandle
{
public:
    RDGRayTracingPassHandle(uint64_t id = UINT64_MAX) : RDGPassHandle(id) {};
};

class RDGPresentPassHandle : public RDGPassHandle
{
public:
    RDGPresentPassHandle(uint64_t id = UINT64_MAX) : RDGPassHandle(id) {};
};

class RDGCopyPassHandle : public RDGPassHandle
{
public:
    RDGCopyPassHandle(uint64_t id = UINT64_MAX) : RDGPassHandle(id) {};
};

class RDGTextureHandle : public RDGResoruceHandle
{
public:
    RDGTextureHandle(uint64_t id = UINT64_MAX) : RDGResoruceHandle(id) {};
};

class RDGBufferHandle : public RDGResoruceHandle
{
public:
    RDGBufferHandle(uint64_t id = UINT64_MAX) : RDGResoruceHandle(id) {};
};
