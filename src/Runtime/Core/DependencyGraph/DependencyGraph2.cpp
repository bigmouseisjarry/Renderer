
#include "DependencyGraph2.h"

#include "lemon/list_graph.h"

using namespace lemon;
class DependencyGraphImpl : public DependencyGraph
{
public:
    using DAGVertex = ListDigraph::Node;
    using DAGEdge = ListDigraph::Arc;
    using DAGVertMap = ListDigraph::NodeMap<Node*>;
    using DAGEdgeMap = ListDigraph::ArcMap<Edge*>;
    using DAG = ListDigraph;
    DependencyGraphImpl() 
        : vert_map(graph), edge_map(graph)
    {

    }
    DAG graph;
    DAGVertMap vert_map;
    DAGEdgeMap edge_map;

    virtual uint64_t insert(Node* node)  final
    {
        const auto dag_node = graph.addNode();
        node->id = graph.id(dag_node);
        node->graph = this;
        vert_map.set(dag_node, node);
        node->on_insert();
        return node->id;
    }

    virtual Node* access_node(uint64_t id)  final
    {
        const auto dag_node = graph.nodeFromId((int)id);
        return vert_map[dag_node];
    }

    virtual bool remove(uint64_t id)  final
    {
        auto dag_node = graph.nodeFromId((int)id);
        vert_map[dag_node]->on_remove();
        graph.erase(dag_node);
        return true;
    }

    virtual bool remove(Node* node)  final
    {
        return remove(node->id);
    }

    virtual bool clear()  final
    {
        graph.clear();
        return true;
    }

    virtual bool link(Node* from, Node* to, Edge* edge)  final
    {
        const auto from_node = graph.nodeFromId((int)from->get_id());
        const auto to_node = graph.nodeFromId((int)to->get_id());
        const auto dag_arc = graph.addArc(from_node, to_node);
        if (edge)
        {
            edge->graph = this;
            edge->from_node = from->get_id();
            edge->to_node = to->get_id();
            edge_map.set(dag_arc, edge);
            edge->on_link();
            return edge;
        }
        return false;
    }

    /*
        virtual Edge* linkage(Node* from, Node* to)  final
        {
            return linkage(from->id, to->id);
        }

        virtual Edge* linkage(uint64_t from, uint64_t to)  final
        {
            graph.addArc(Node s, Node t)
            auto find_edge = boost::edge((vertex_descriptor)from, (vertex_descriptor)to, *this);
            if (find_edge.second)
            {
                return (*this)[find_edge.first];
            }
            return nullptr;
        }

        virtual bool unlink(Node* from, Node* to)  final
        {
            return unlink(from->id, to->id);
        }

        virtual bool unlink(uint64_t from, uint64_t to)  final
        {
            auto find_edge = boost::edge((vertex_descriptor)from, (vertex_descriptor)to, *this);
            if (!find_edge.second) return false;
            (*this)[find_edge.first]->on_unlink();
            boost::remove_edge(find_edge.first, *this);
            return true;
        }
    */

    virtual Node* from_node(Edge* edge)  final
    {
        return access_node(edge->from_node);
    }

    virtual Node* to_node(Edge* edge)  final
    {
        return access_node(edge->to_node);
    }

    virtual uint32_t foreach_neighbors(uint64_t id, std::function<void(DependencyGraphNode*)> f)  final
    {
        const auto node = graph.nodeFromId((int)id);
        uint32_t count = 0;
        for (ListDigraph::OutArcIt arcIt(graph, node); arcIt != INVALID; ++arcIt)
        {
            auto nid = graph.target(arcIt);
            auto node = vert_map[nid];
            f(node);
            count++;
        }
        return count;
    }

    virtual uint32_t foreach_neighbors(const uint64_t id, std::function<void(const DependencyGraphNode*)> f) const  final
    {
        const auto node = graph.nodeFromId((int)id);
        uint32_t count = 0;
        for (ListDigraph::OutArcIt arcIt(graph, node); arcIt != INVALID; ++arcIt)
        {
            auto nid = graph.target(arcIt);
            auto node = vert_map[nid];
            f(node);
            count++;
        }
        return count;
    }

    virtual uint32_t foreach_neighbors(Node* node, std::function<void(DependencyGraphNode*)> f)  final
    {
        return foreach_neighbors(node->get_id(), f);
    }

    virtual uint32_t foreach_neighbors(const Node* node, std::function<void(const DependencyGraphNode*)> f) const  final
    {
        return foreach_neighbors(node->get_id(), f);
    }

    virtual uint32_t foreach_inv_neighbors(uint64_t id, std::function<void(DependencyGraphNode*)> f)  final
    {
        const auto node = graph.nodeFromId((int)id);
        uint32_t count = 0;
        for (ListDigraph::InArcIt arcIt(graph, node); arcIt != INVALID; ++arcIt)
        {
            auto nid = graph.source(arcIt);
            auto node = vert_map[nid];
            f(node);
            count++;
        }
        return count;
    }

    virtual uint32_t foreach_inv_neighbors(const uint64_t id, std::function<void(const DependencyGraphNode*)> f) const  final
    {
        const auto node = graph.nodeFromId((int)id);
        uint32_t count = 0;
        for (ListDigraph::InArcIt arcIt(graph, node); arcIt != INVALID; ++arcIt)
        {
            auto nid = graph.source(arcIt);
            auto node = vert_map[nid];
            f(node);
            count++;
        }
        return count;
    }

    virtual uint32_t foreach_inv_neighbors(Node* node, std::function<void(DependencyGraphNode*)> f)  final
    {
        return foreach_inv_neighbors(node->get_id(), f);
    }

    virtual uint32_t foreach_inv_neighbors(const Node* node, std::function<void(const DependencyGraphNode*)> f) const  final
    {
        return foreach_inv_neighbors(node->get_id(), f);
    }

    virtual uint32_t foreach_outgoing_edges(uint64_t id, std::function<void(Node* from, Node* to, Edge* edge)> f)  final
    {
        const auto node = graph.nodeFromId((int)id);
        uint32_t count = 0;
        for (ListDigraph::OutArcIt arcIt(graph, node); arcIt != INVALID; ++arcIt)
        {
            auto edge = edge_map[arcIt];
            f(edge->from(), edge->to(), edge);
            count++;
        }
        return count;
    }

    virtual uint32_t foreach_outgoing_edges(Node* node, std::function<void(Node* from, Node* to, Edge* edge)> func)  final
    {
        return foreach_outgoing_edges(node->id, func);
    }

    virtual uint32_t foreach_incoming_edges(uint64_t id, std::function<void(Node* from, Node* to, Edge* edge)> f)  final
    {
        const auto node = graph.nodeFromId((int)id);
        uint32_t count = 0;
        for (ListDigraph::InArcIt arcIt(graph, node); arcIt != INVALID; ++arcIt)
        {
            auto edge = edge_map[arcIt];
            f(edge->from(), edge->to(), edge);
            count++;
        }
        return count;
    }

    virtual uint32_t foreach_incoming_edges(Node* node,
        std::function<void(Node* from, Node* to, Edge* edge)> func)  final
    {
        return foreach_incoming_edges(node->id, func);
    }

    virtual uint32_t foreach_edges(std::function<void(Node* from, Node* to, Edge* edge)> f)  final
    {
        uint32_t count = 0;
        for (ListDigraph::ArcIt arcIt(graph); arcIt != INVALID; ++arcIt)
        {
            auto edge = edge_map[arcIt];
            f(edge->from(), edge->to(), edge);
            count++;
        }
        return count;
    }

    virtual uint32_t outgoing_edges(uint64_t id)  final
    {
        const auto node = graph.nodeFromId((int)id);
        uint32_t count = 0;
        for (ListDigraph::OutArcIt arcIt(graph, node); arcIt != INVALID; ++arcIt)
        {
            count++;
        }
        return count;
    }

    virtual uint32_t outgoing_edges(const Node* node)  final
    {
        return outgoing_edges(node->id);
    }

    virtual uint32_t incoming_edges(const Node* node)  final
    {
        return incoming_edges(node->id);
    }

    virtual uint32_t incoming_edges(uint64_t id)  final
    {
        const auto node = graph.nodeFromId((int)id);
        uint32_t count = 0;
        for (ListDigraph::InArcIt arcIt(graph, node); arcIt != INVALID; ++arcIt)
        {
            count++;
        }
        return count;
    }
};

uint32_t DependencyGraphNode::outgoing_edges() 
{
    return graph->outgoing_edges(this);
}

uint32_t DependencyGraphNode::incoming_edges() 
{
    return graph->incoming_edges(this);
}

uint32_t DependencyGraphNode::foreach_incoming_edges(std::function<void(DependencyGraphNode* from, DependencyGraphNode* to, DependencyGraphEdge* edge)> f) 
{
    return graph->foreach_incoming_edges(this, f);
}

uint32_t DependencyGraphNode::foreach_outgoing_edges(std::function<void(DependencyGraphNode* from, DependencyGraphNode* another, DependencyGraphEdge* edge)> f) 
{
    return graph->foreach_outgoing_edges(this, f);
}

uint32_t DependencyGraphNode::foreach_neighbors(std::function<void(DependencyGraphNode* neig)> f) 
{
    return graph->foreach_neighbors(this, f);
}

uint32_t DependencyGraphNode::foreach_neighbors(std::function<void(const DependencyGraphNode* neig)> f) const 
{
    return graph->foreach_neighbors(this, f);
}

uint32_t DependencyGraphNode::foreach_inv_neighbors(std::function<void(DependencyGraphNode* inv_neig)> f) 
{
    return graph->foreach_inv_neighbors(this, f);
}

uint32_t DependencyGraphNode::foreach_inv_neighbors(std::function<void(const DependencyGraphNode* inv_neig)> f) const 
{
    return graph->foreach_inv_neighbors(this, f);
}


void DependencyGraph::Destroy(DependencyGraph* graph) 
{
    delete graph;
}

DependencyGraph* DependencyGraph::Create() 
{
    return new DependencyGraphImpl();
}

