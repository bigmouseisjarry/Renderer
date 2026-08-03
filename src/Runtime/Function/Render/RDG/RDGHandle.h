#pragma once

#include "Core/DependencyGraph/DependencyGraph.h"

#include <cstdint>

using NodeID = DependencyGraph::NodeID;

class RDGResoruceHandle
{
public:
    RDGResoruceHandle(NodeID id) : id(id) {}

    bool operator< (const RDGResoruceHandle& other) const noexcept {
        return id < other.id;
    }

    bool operator== (const RDGResoruceHandle& other) const noexcept {
        return (id == other.id);
    }

    bool operator!= (const RDGResoruceHandle& other) const noexcept {
        return !operator==(other);
    }

    inline NodeID ID() { return id; }

protected:
    NodeID id = UINT32_MAX;
};

class RDGPassHandle : public RDGResoruceHandle
{
public:
    RDGPassHandle(NodeID id = UINT32_MAX) : RDGResoruceHandle(id) {};
};

class RDGRenderPassHandle : public RDGPassHandle
{
public:
    RDGRenderPassHandle(NodeID id = UINT32_MAX) : RDGPassHandle(id) {};
};

class RDGComputePassHandle : public RDGPassHandle
{
public:
    RDGComputePassHandle(NodeID id = UINT32_MAX) : RDGPassHandle(id) {};
};

class RDGRayTracingPassHandle : public RDGPassHandle
{
public:
    RDGRayTracingPassHandle(NodeID id = UINT32_MAX) : RDGPassHandle(id) {};
};

class RDGPresentPassHandle : public RDGPassHandle
{
public:
    RDGPresentPassHandle(NodeID id = UINT32_MAX) : RDGPassHandle(id) {};
};

class RDGCopyPassHandle : public RDGPassHandle
{
public:
    RDGCopyPassHandle(NodeID id = UINT32_MAX) : RDGPassHandle(id) {};
};

class RDGTextureHandle : public RDGResoruceHandle
{
public:
    RDGTextureHandle(NodeID id = UINT32_MAX) : RDGResoruceHandle(id) {};
};

class RDGBufferHandle : public RDGResoruceHandle
{
public:
    RDGBufferHandle(NodeID id = UINT32_MAX) : RDGResoruceHandle(id) {};
};
