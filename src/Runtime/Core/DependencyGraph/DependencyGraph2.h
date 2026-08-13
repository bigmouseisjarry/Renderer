#pragma once

#include <cstdint>
#include <functional>

class DependencyGraphEdge;

// 图节点
class DependencyGraphNode
{
    friend class DependencyGraphImpl;

public:
    DependencyGraphNode() = default;
    using Type = DependencyGraphNode;
    virtual ~DependencyGraphNode()  = default;
    // 节点禁止拷贝
    DependencyGraphNode(const Type&)  = delete;

    // 生命周期钩子
    virtual void on_insert()  {}
    virtual void on_remove()  {}

    // ID 访问
    const uint64_t get_id() const  { return id; }

    // 查询接口
    uint32_t outgoing_edges() ;
    uint32_t incoming_edges() ;
    uint32_t foreach_incoming_edges(std::function<void(DependencyGraphNode* from, DependencyGraphNode* to, DependencyGraphEdge* edge)>) ;
    uint32_t foreach_outgoing_edges(std::function<void(DependencyGraphNode* from, DependencyGraphNode* to, DependencyGraphEdge* edge)>) ;

    // 正向邻居，即出边指向的节点
    uint32_t foreach_neighbors(std::function<void(DependencyGraphNode* neig)>) ;
    uint32_t foreach_neighbors(std::function<void(const DependencyGraphNode* neig)>) const ;

    // 反向邻居，即入边来源的节点
    uint32_t foreach_inv_neighbors(std::function<void(DependencyGraphNode* inv_neig)>) ;
    uint32_t foreach_inv_neighbors(std::function<void(const DependencyGraphNode* inv_neig)>) const ;

private:
    class DependencyGraph* graph;
    // 由图写入的在图中的唯一 ID
    uint64_t id;
};

// 图的边
class DependencyGraphEdge
{
    friend class DependencyGraphImpl;

public:
    DependencyGraphEdge() = default;
    using Type = DependencyGraphEdge;
    virtual ~DependencyGraphEdge()  = default;
    // Edges can't be copied
    DependencyGraphEdge(const Type&)  = delete;
    virtual void on_link()  {}
    virtual void on_unlink()  {}
    DependencyGraphNode* from() ;
    DependencyGraphNode* to() ;

protected:
    class DependencyGraph* graph;
    uint64_t from_node;
    uint64_t to_node;
};

// 图
class DependencyGraph
{
public:
    using Node = DependencyGraphNode;
    using Edge = DependencyGraphEdge;
    static DependencyGraph* Create() ;
    static void Destroy(DependencyGraph* graph) ;
    virtual ~DependencyGraph()  = default;
    virtual uint64_t insert(Node* node)  = 0;
    virtual Node* access_node(uint64_t handle)  = 0;
    virtual bool remove(uint64_t node)  = 0;
    virtual bool remove(Node* node)  = 0;
    virtual bool clear()  = 0;
    virtual bool link(Node* from, Node* to, Edge* edge = nullptr)  = 0;
    virtual Node* from_node(Edge* edge)  = 0;
    virtual Node* to_node(Edge* edge)  = 0;
    virtual uint32_t foreach_neighbors(Node* node, std::function<void(Node* neig)>)  = 0;
    virtual uint32_t foreach_neighbors(uint64_t node, std::function<void(Node* neig)>)  = 0;
    virtual uint32_t foreach_neighbors(const Node* node, std::function<void(const Node* neig)>) const  = 0;
    virtual uint32_t foreach_neighbors(const uint64_t node, std::function<void(const Node* neig)>) const  = 0;
    virtual uint32_t foreach_inv_neighbors(Node* node, std::function<void(Node* inv_neig)>)  = 0;
    virtual uint32_t foreach_inv_neighbors(uint64_t node, std::function<void(Node* inv_neig)>)  = 0;
    virtual uint32_t foreach_inv_neighbors(const Node* node, std::function<void(const Node* inv_neig)>) const  = 0;
    virtual uint32_t foreach_inv_neighbors(const uint64_t node, std::function<void(const Node* inv_neig)>) const  = 0;
    virtual uint32_t outgoing_edges(const Node* node)  = 0;
    virtual uint32_t outgoing_edges(uint64_t id)  = 0;
    virtual uint32_t foreach_outgoing_edges(uint64_t node,std::function<void(Node* from, Node* to, Edge* edge)>)  = 0;
    virtual uint32_t foreach_outgoing_edges(Node* node,std::function<void(Node* from, Node* to, Edge* edge)>)  = 0;
    virtual uint32_t incoming_edges(const Node* node)  = 0;
    virtual uint32_t incoming_edges(uint64_t id)  = 0;
    virtual uint32_t foreach_incoming_edges(Node* node,
        std::function<void(Node* from, Node* to, Edge* edge)>)  = 0;
    virtual uint32_t foreach_incoming_edges(uint64_t node,
        std::function<void(Node* from, Node* to, Edge* edge)>)  = 0;
    virtual uint32_t foreach_edges(std::function<void(Node* from, Node* to, Edge* edge)>)  = 0;
};

inline DependencyGraphNode* DependencyGraphEdge::from() 
{
    return graph->access_node(from_node);
}
inline DependencyGraphNode* DependencyGraphEdge::to() 
{
    return graph->access_node(to_node);
}

