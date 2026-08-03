
#include "RDGGraphWidget.h"
#include "Function/Render/RDG/RDGEdge.h"
#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RDG/RDGPool.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Texture.h"
#include "Core/DependencyGraph/DependencyGraph.h"
#include "Function/Global/EngineContext.h"
#include <memory>
#include <regex>
#include <set>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_node_editor.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>

namespace ed = ax::NodeEditor;

static const int PIN_ICON_SIZE = 24;

enum class PinType
{
    Flow,
    InResource,
    OutResource,
};

enum class NodeType
{
    Texture, 
    Buffer,

    RenderPass,
    ComputePass,
    RayTracingPass,
    PresentPass,
    CopyPass,
};

struct GraphNode;

typedef struct GraphLink
{
    ed::LinkId id;
 
    ed::PinId from;
    ed::PinId to;

    RDGEdgeRef rdgEdge;

} GraphLink;

typedef struct GraphPin
{
    ed::PinId id;
    ed::PinKind kind;
    std::vector<GraphLink*> pinLinks;  
    GraphNode* node;
    PinType type;

} GraphPin;

typedef struct GraphNode
{
    ed::NodeId id;
    std::string name;
    std::vector<GraphPin> inputs;
    std::vector<GraphPin> outputs;
    NodeType type;

    RDGResourceNodeRef rdgResourceNode;
    RDGPassNodeRef rdgPassNode;

} GraphNode;

enum class IconType: ImU32 
{ 
    Flow, 
    Circle, 
    Square, 
    Grid, 
    RoundSquare, 
    Diamond 
};

void DrawIcon(
    ImDrawList* drawList, 
    const ImVec2& a, 
    const ImVec2& b, 
    IconType type, 
    bool filled, 
    ImU32 color, 
    ImU32 innerColor)
{
          auto rect           = ImRect(a, b);
          auto rect_x         = rect.Min.x;
          auto rect_y         = rect.Min.y;
          auto rect_w         = rect.Max.x - rect.Min.x;
          auto rect_h         = rect.Max.y - rect.Min.y;
          auto rect_center_x  = (rect.Min.x + rect.Max.x) * 0.5f;
          auto rect_center_y  = (rect.Min.y + rect.Max.y) * 0.5f;
          auto rect_center    = ImVec2(rect_center_x, rect_center_y);
    const auto outline_scale  = rect_w / 24.0f;
    const auto extra_segments = static_cast<int>(2 * outline_scale); // for full circle

    if (type == IconType::Flow)
    {
        const auto origin_scale = rect_w / 24.0f;

        const auto offset_x  = 1.0f * origin_scale;
        const auto offset_y  = 0.0f * origin_scale;
        const auto margin     = (filled ? 2.0f : 2.0f) * origin_scale;
        const auto rounding   = 0.1f * origin_scale;
        const auto tip_round  = 0.7f; // percentage of triangle edge (for tip)
        //const auto edge_round = 0.7f; // percentage of triangle edge (for corner)
        const auto canvas = ImRect(
            rect.Min.x + margin + offset_x,
            rect.Min.y + margin + offset_y,
            rect.Max.x - margin + offset_x,
            rect.Max.y - margin + offset_y);
        const auto canvas_x = canvas.Min.x;
        const auto canvas_y = canvas.Min.y;
        const auto canvas_w = canvas.Max.x - canvas.Min.x;
        const auto canvas_h = canvas.Max.y - canvas.Min.y;

        const auto left   = canvas_x + canvas_w            * 0.5f * 0.3f;
        const auto right  = canvas_x + canvas_w - canvas_w * 0.5f * 0.3f;
        const auto top    = canvas_y + canvas_h            * 0.5f * 0.2f;
        const auto bottom = canvas_y + canvas_h - canvas_h * 0.5f * 0.2f;
        const auto center_y = (top + bottom) * 0.5f;
        //const auto angle = AX_PI * 0.5f * 0.5f * 0.5f;

        const auto tip_top    = ImVec2(canvas_x + canvas_w * 0.5f, top);
        const auto tip_right  = ImVec2(right, center_y);
        const auto tip_bottom = ImVec2(canvas_x + canvas_w * 0.5f, bottom);

        drawList->PathLineTo(ImVec2(left, top) + ImVec2(0, rounding));
        drawList->PathBezierCubicCurveTo(
            ImVec2(left, top),
            ImVec2(left, top),
            ImVec2(left, top) + ImVec2(rounding, 0));
        drawList->PathLineTo(tip_top);
        drawList->PathLineTo(tip_top + (tip_right - tip_top) * tip_round);
        drawList->PathBezierCubicCurveTo(
            tip_right,
            tip_right,
            tip_bottom + (tip_right - tip_bottom) * tip_round);
        drawList->PathLineTo(tip_bottom);
        drawList->PathLineTo(ImVec2(left, bottom) + ImVec2(rounding, 0));
        drawList->PathBezierCubicCurveTo(
            ImVec2(left, bottom),
            ImVec2(left, bottom),
            ImVec2(left, bottom) - ImVec2(0, rounding));

        if (!filled)
        {
            if (innerColor & 0xFF000000)
                drawList->AddConvexPolyFilled(drawList->_Path.Data, drawList->_Path.Size, innerColor);

            drawList->PathStroke(color, true, 2.0f * outline_scale);
        }
        else
            drawList->PathFillConvex(color);
    }
    else
    {
        auto triangleStart = rect_center_x + 0.32f * rect_w;

        auto rect_offset = -static_cast<int>(rect_w * 0.25f * 0.25f);

        rect.Min.x    += rect_offset;
        rect.Max.x    += rect_offset;
        rect_x        += rect_offset;
        rect_center_x += rect_offset * 0.5f;
        rect_center.x += rect_offset * 0.5f;

        if (type == IconType::Circle)
        {
            const auto c = rect_center;

            if (!filled)
            {
                const auto r = 0.5f * rect_w / 2.0f - 0.5f;

                if (innerColor & 0xFF000000)
                    drawList->AddCircleFilled(c, r, innerColor, 12 + extra_segments);
                drawList->AddCircle(c, r, color, 12 + extra_segments, 2.0f * outline_scale);
            }
            else
            {
                drawList->AddCircleFilled(c, 0.5f * rect_w / 2.0f, color, 12 + extra_segments);
            }
        }

        if (type == IconType::Square)
        {
            if (filled)
            {
                const auto r  = 0.5f * rect_w / 2.0f;
                const auto p0 = rect_center - ImVec2(r, r);
                const auto p1 = rect_center + ImVec2(r, r);

#if IMGUI_VERSION_NUM > 18101
                drawList->AddRectFilled(p0, p1, color, 0, ImDrawFlags_RoundCornersAll);
#else
                drawList->AddRectFilled(p0, p1, color, 0, 15);
#endif
            }
            else
            {
                const auto r = 0.5f * rect_w / 2.0f - 0.5f;
                const auto p0 = rect_center - ImVec2(r, r);
                const auto p1 = rect_center + ImVec2(r, r);

                if (innerColor & 0xFF000000)
                {
#if IMGUI_VERSION_NUM > 18101
                    drawList->AddRectFilled(p0, p1, innerColor, 0, ImDrawFlags_RoundCornersAll);
#else
                    drawList->AddRectFilled(p0, p1, innerColor, 0, 15);
#endif
                }

#if IMGUI_VERSION_NUM > 18101
                drawList->AddRect(p0, p1, color, 0, ImDrawFlags_RoundCornersAll, 2.0f * outline_scale);
#else
                drawList->AddRect(p0, p1, color, 0, 15, 2.0f * outline_scale);
#endif
            }
        }

        if (type == IconType::Grid)
        {
            const auto r = 0.5f * rect_w / 2.0f;
            const auto w = ceilf(r / 3.0f);

            const auto baseTl = ImVec2(floorf(rect_center_x - w * 2.5f), floorf(rect_center_y - w * 2.5f));
            const auto baseBr = ImVec2(floorf(baseTl.x + w), floorf(baseTl.y + w));

            auto tl = baseTl;
            auto br = baseBr;
            for (int i = 0; i < 3; ++i)
            {
                tl.x = baseTl.x;
                br.x = baseBr.x;
                drawList->AddRectFilled(tl, br, color);
                tl.x += w * 2;
                br.x += w * 2;
                if (i != 1 || filled)
                    drawList->AddRectFilled(tl, br, color);
                tl.x += w * 2;
                br.x += w * 2;
                drawList->AddRectFilled(tl, br, color);

                tl.y += w * 2;
                br.y += w * 2;
            }

            triangleStart = br.x + w + 1.0f / 24.0f * rect_w;
        }

        if (type == IconType::RoundSquare)
        {
            if (filled)
            {
                const auto r  = 0.5f * rect_w / 2.0f;
                const auto cr = r * 0.5f;
                const auto p0 = rect_center - ImVec2(r, r);
                const auto p1 = rect_center + ImVec2(r, r);

#if IMGUI_VERSION_NUM > 18101
                drawList->AddRectFilled(p0, p1, color, cr, ImDrawFlags_RoundCornersAll);
#else
                drawList->AddRectFilled(p0, p1, color, cr, 15);
#endif
            }
            else
            {
                const auto r = 0.5f * rect_w / 2.0f - 0.5f;
                const auto cr = r * 0.5f;
                const auto p0 = rect_center - ImVec2(r, r);
                const auto p1 = rect_center + ImVec2(r, r);

                if (innerColor & 0xFF000000)
                {
#if IMGUI_VERSION_NUM > 18101
                    drawList->AddRectFilled(p0, p1, innerColor, cr, ImDrawFlags_RoundCornersAll);
#else
                    drawList->AddRectFilled(p0, p1, innerColor, cr, 15);
#endif
                }

#if IMGUI_VERSION_NUM > 18101
                drawList->AddRect(p0, p1, color, cr, ImDrawFlags_RoundCornersAll, 2.0f * outline_scale);
#else
                drawList->AddRect(p0, p1, color, cr, 15, 2.0f * outline_scale);
#endif
            }
        }
        else if (type == IconType::Diamond)
        {
            if (filled)
            {
                const auto r = 0.607f * rect_w / 2.0f;
                const auto c = rect_center;

                drawList->PathLineTo(c + ImVec2( 0, -r));
                drawList->PathLineTo(c + ImVec2( r,  0));
                drawList->PathLineTo(c + ImVec2( 0,  r));
                drawList->PathLineTo(c + ImVec2(-r,  0));
                drawList->PathFillConvex(color);
            }
            else
            {
                const auto r = 0.607f * rect_w / 2.0f - 0.5f;
                const auto c = rect_center;

                drawList->PathLineTo(c + ImVec2( 0, -r));
                drawList->PathLineTo(c + ImVec2( r,  0));
                drawList->PathLineTo(c + ImVec2( 0,  r));
                drawList->PathLineTo(c + ImVec2(-r,  0));

                if (innerColor & 0xFF000000)
                    drawList->AddConvexPolyFilled(drawList->_Path.Data, drawList->_Path.Size, innerColor);

                drawList->PathStroke(color, true, 2.0f * outline_scale);
            }
        }
        else
        {
            const auto triangleTip = triangleStart + rect_w * (0.45f - 0.32f);

            drawList->AddTriangleFilled(
                ImVec2(ceilf(triangleTip), rect_y + rect_h * 0.5f),
                ImVec2(triangleStart, rect_center_y + 0.15f * rect_h),
                ImVec2(triangleStart, rect_center_y - 0.15f * rect_h),
                color);
        }
    }
}

void Icon(
    const ImVec2& size, 
    IconType type, 
    bool filled, 
    const ImVec4& color = ImVec4(1, 1, 1, 1), 
    const ImVec4& innerColor = ImVec4(0, 0, 0, 0))
{
    if (ImGui::IsRectVisible(size))
    {
        auto cursorPos = ImGui::GetCursorScreenPos();
        auto drawList  = ImGui::GetWindowDrawList();
        DrawIcon(drawList, cursorPos, cursorPos + size, type, filled, ImColor(color), ImColor(innerColor));
    }

    ImGui::Dummy(size);
}

void DrawPinIcon(const GraphPin& pin, int alpha, bool filled)
{
    ImColor color = ImColor(255, 255, 255, 255);
    switch (pin.type)
    {
        case PinType::Flow:         color = ImColor(255, 255, 255); break;
        case PinType::InResource:   color = ImColor( 51, 150, 215); break;
        case PinType::OutResource:  color = ImColor(218,   0, 183); break;
        default:                                                            return;
    }
    color.Value.w = alpha / 255.0f;

    IconType iconType;  
    switch (pin.type)
    {
        case PinType::Flow:         iconType = IconType::Flow;   break;
        case PinType::InResource:   iconType = IconType::Circle; break;
        case PinType::OutResource:  iconType = IconType::Circle; break;
        default:                                                 return;
    }

    Icon(
        ImVec2(static_cast<float>(PIN_ICON_SIZE), static_cast<float>(PIN_ICON_SIZE)), 
        iconType, 
        filled, 
        color, 
        ImColor(32, 32, 32, alpha));
};


void DebugBoundingBox()
{
     ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(),
                ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 255)); 
}

void PinUI(const GraphPin& pin, bool filled)
{
    auto alpha = ImGui::GetStyle().Alpha;
    ed::BeginPin(pin.id, pin.kind);
    ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
    ed::PinPivotSize(ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
 
    DrawPinIcon(pin, (int)(alpha * 255), filled);
    // ImGui::SameLine();
    // ImGui::Text("text");
    ImGui::PopStyleVar();
    ed::EndPin();
}

void LinkUI(const GraphLink& link)
{
    ed::Link(
        link.id, 
        link.from,
        link.to, 
        ImColor(255, 255, 255, 255), 
        2.0f);
}

void ResourceNodeHoverUI(const GraphNode& node)
{
    // if(node.rdgPassNode)
    // {
    //     switch (node.rdgPassNode->NodeType()) {
    //     case RDG_PASS_NODE_TYPE_RENDER:
    //     case RDG_PASS_NODE_TYPE_COMPUTE:
    //     case RDG_PASS_NODE_TYPE_RAY_TRACING:
    //     case RDG_PASS_NODE_TYPE_PRESENT:
    //     case RDG_PASS_NODE_TYPE_COPY:
    //     default: break;
    //     }
    // }
    // else 
    if (node.rdgResourceNode) {
        switch (node.rdgResourceNode->NodeType()) {
        case RDG_RESOURCE_NODE_TYPE_TEXTURE:
        {
            RDGTextureNodeRef textureNode = static_cast<RDGTextureNodeRef>(node.rdgResourceNode);
            
            auto& info = textureNode->GetInfo();
            Extent3D extent = info.extent;
            ImGui::Text("Extent: %dx%dx%d", extent.width, extent.height, extent.depth); 
            ImGui::Text("Format: %d", info.format);
            ImGui::Text("Miplevel: %d", info.mipLevels);
            ImGui::Text("Array Layers: %d", info.arrayLayers);

            if(textureNode->IsImported()) 
            {
                ImGui::Separator();
                ImGui::Text("[Impoted Resource]");
            }  

            break;
        }
        case RDG_RESOURCE_NODE_TYPE_BUFFER:
        {
            RDGBufferNodeRef bufferNode = static_cast<RDGBufferNodeRef>(node.rdgResourceNode);

            auto& info = bufferNode->GetInfo();
            ImGui::Text("Size: %llu", info.size);
            ImGui::Text("Memory usage: %d", info.memoryUsage);

            if(bufferNode->IsImported()) 
            {
                ImGui::Separator();
                ImGui::Text("[Impoted Resource]");
            } 
            
            break;
        }
        default: break;
        }
    }
}

void LinkHoverUI(const GraphLink& link)
{
    // ImGui::Text("from: %lld", (uint64_t)link.from.AsPointer());
    // ImGui::Text("to: %lld", (uint64_t)link.to.AsPointer());

    if(link.rdgEdge)
    {
        switch (link.rdgEdge->EdgeType()) {

        case RDG_EDGE_TYPE_TEXTURE:
        {
            RDGTextureEdgeRef textureEdge = static_cast<RDGTextureEdgeRef>(link.rdgEdge);
            if(textureEdge->asColor)            ImGui::Text("Color Attachement");
            if(textureEdge->asDepthStencil)     ImGui::Text("Depth Stencil Attachement");
            if(textureEdge->asPresent)          ImGui::Text("Present");
            if(textureEdge->asShaderRead)       ImGui::Text("Shader read");
            if(textureEdge->asShaderReadWrite)  ImGui::Text("Shader Read Write");
            if(textureEdge->asTransferSrc)      ImGui::Text("Transfer Src");
            if(textureEdge->asTransferDst)      ImGui::Text("Transfer Dst");
            break;
        }
        case RDG_EDGE_TYPE_BUFFER:
        {
            RDGBufferEdgeRef bufferEdge = static_cast<RDGBufferEdgeRef>(link.rdgEdge);
            if(bufferEdge->asShaderRead)        ImGui::Text("Shader read");
            if(bufferEdge->asShaderReadWrite)   ImGui::Text("Shader Read Write");
            break;
        }
        default: break;
        }
    }
}

void RDGGraphWidget::UI()
{
    static bool open = false;
    if(ImGui::Button("Show Render Dependency Graph"))
    {
        open = true;
    }

    if(open) UIInternel(&open);
}

void RDGGraphWidget::UIInternel(bool* open)
{ 
    // 预构建的图表信息
    static std::vector<GraphNode> passNodes;
    static std::vector<GraphNode> resourceNodes;
    static std::vector<GraphLink> links; 
    static std::unordered_map<std::string, GraphNode*> passNodesMap;
    static std::unordered_map<std::string, GraphNode*> resourceNodesMap;
    static std::unordered_map<uint32_t, GraphNode*> rdgNodeIdToNodes;   // 此处的ID是dependency graph的id，多个重名的会用同一个
    static bool autoUpdate = false;
    static int autoUpdateCount = 0;
    
    // 统计信息
    static uint32_t importedResourceCount = 0;
    static uint32_t importedUniqueResourceCount = 0;    // 移除了重名的部分

    // 选中标记
    static ed::NodeId contextNodeId;
    static ed::LinkId contextLinkId;

    static uint32_t currentFrameIndex = 0;
    static DependencyGraphRef rdgDependencyGraph = nullptr;
    static ed::EditorContext* context = nullptr;
    if(context == nullptr)
    {
        ed::Config config = {};
        config.SettingsFile = "Simple.json";        // 以执行路径为基准
        config.NavigateButtonIndex = 2;                 // 中键拖动
        context = ed::CreateEditor(&config);
    }
    int nodeUniqueId = 1;
    int pinUniqueId = 1e3;
    int linkUniqueId = 1e6;
    bool init = false;
    ImVec2 nodePosition = ImVec2(0, 0);
    float nodePositionY = 0;

    if(!ImGui::Begin("RDG##1", open))
    {
        ImGui::End();
        return;
    }

    {
        if(autoUpdate) autoUpdateCount++;
        if(ImGui::Button("Refresh") || rdgDependencyGraph == nullptr || autoUpdateCount > 100)
        {
            currentFrameIndex = EngineContext::ThreadPool()->ThreadFrameIndex();
            rdgDependencyGraph = EngineContext::Render()->GetRDGDependenctyGraph();
            init = true;
            autoUpdateCount = 0;
        }
        else init = false;
        if(rdgDependencyGraph == nullptr) return;

        ImGui::SameLine();
        ImGui::Checkbox("Auto update every 100 frames", &autoUpdate);

        ImGui::Text("Resource count: %d / %d, imported count: %d / %d", 
            (int)resourceNodes.size(),
            (int)rdgDependencyGraph->GetNodes<RDGResourceNode>().size(),
            importedUniqueResourceCount,
            importedResourceCount);
        
        ImGui::Text("Pass count: %d / %d",
            (int)passNodes.size(), 
            (int)rdgDependencyGraph->GetNodes<RDGPassNode>().size());

        ImGui::Text("Resource pool allocated count: %d(buffer) / %d(texture) / %d(view) / %d(descriptor set)", 
            RDGBufferPool::Get()->AllocatedSize(), 
            RDGTexturePool::Get()->AllocatedSize(),
            RDGTextureViewPool::Get()->AllocatedSize(),
            RDGDescriptorSetPool::Get(currentFrameIndex)->AllocatedSize());

        
        
        if(init)
        {
            passNodes.clear();
            resourceNodes.clear();
            links.clear();
            passNodesMap.clear();
            resourceNodesMap.clear();
            rdgNodeIdToNodes.clear();
            importedResourceCount = 0;
            importedUniqueResourceCount = 0;
            
            links.reserve(rdgDependencyGraph->GetEdges().size());

            // 初始化资源结点信息
            auto nodes0 = rdgDependencyGraph->GetNodes<RDGResourceNode>();
            resourceNodes.reserve(nodes0.size());
            for(auto& resourceNode : nodes0)
            {
                if(resourceNode->IsImported()) importedResourceCount++;

                std::regex pattern0("\\s\\[\\d+\\]\\[\\d+\\]");
                std::regex pattern1("\\s\\[\\d+\\]");
                std::string name = std::regex_replace(resourceNode->Name(), pattern0, "");
                name = std::regex_replace(name, pattern1, "");

                if(resourceNodesMap.find(name) == resourceNodesMap.end())
                {
                    if(resourceNode->IsImported()) importedUniqueResourceCount++;

                    NodeType nodeType;
                    auto rdgNodeType = resourceNode->NodeType();
                    switch (rdgNodeType) {
                    case RDG_RESOURCE_NODE_TYPE_TEXTURE:        nodeType = NodeType::Texture;       break;
                    case RDG_RESOURCE_NODE_TYPE_BUFFER:         nodeType = NodeType::Buffer;        break;
                    default:                                                                        break;
                    }

                    GraphNode node = {};
                    node.id = nodeUniqueId++;
                    node.name = name;
                    node.type = nodeType;
                    node.rdgResourceNode = resourceNode;
                    
                    GraphPin pin = {};
                    pin.id = pinUniqueId++;
                    pin.node = &node;
                    pin.kind = ed::PinKind::Input;
                    pin.type = PinType::InResource;
                    node.inputs.push_back(pin);

                    pin.id = pinUniqueId++;
                    pin.kind = ed::PinKind::Output;
                    pin.type = PinType::OutResource;
                    node.outputs.push_back(pin);

                    resourceNodes.push_back(node);
                    resourceNodesMap[name] = &resourceNodes.back(); // vector的地址会变，需要预分配尺寸
                }
                rdgNodeIdToNodes[resourceNode->ID()] = resourceNodesMap[name];
            }

            // 初始化pass结点信息    
            auto nodes1 = rdgDependencyGraph->GetNodes<RDGPassNode>();
            passNodes.reserve(nodes1.size());
            GraphNode* previousPassNode = nullptr;
            for(auto& passNode : nodes1)
            {
                std::regex pattern0("\\s\\[\\d+\\]\\[\\d+\\]");
                std::regex pattern1("\\s\\[\\d+\\]");
                std::string name = std::regex_replace(passNode->Name(), pattern0, "");
                name = std::regex_replace(name, pattern1, "");

                if(passNodesMap.find(name) == passNodesMap.end())
                {
                    NodeType nodeType;
                    auto rdgNodeType = passNode->NodeType();
                    switch (rdgNodeType) {
                    case RDG_PASS_NODE_TYPE_RENDER:         nodeType = NodeType::RenderPass;        break;
                    case RDG_PASS_NODE_TYPE_COMPUTE:        nodeType = NodeType::ComputePass;       break;
                    case RDG_PASS_NODE_TYPE_RAY_TRACING:    nodeType = NodeType::RayTracingPass;    break;
                    case RDG_PASS_NODE_TYPE_PRESENT:        nodeType = NodeType::PresentPass;       break;
                    case RDG_PASS_NODE_TYPE_COPY:           nodeType = NodeType::CopyPass;          break;
                    default:                                                                        break;
                    }

                    GraphNode node = {};
                    node.id = nodeUniqueId++;
                    node.name = name;
                    node.type = nodeType;
                    node.rdgPassNode = passNode;
                    
                    GraphPin pin = {};
                    pin.id = pinUniqueId++;
                    pin.node = &node;
                    pin.kind = ed::PinKind::Output;
                    pin.type = PinType::Flow;        
                    node.outputs.push_back(pin);

                    pin.id = pinUniqueId++;
                    pin.kind = ed::PinKind::Input;
                    if(previousPassNode) 
                    {
                        GraphLink link = {};
                        link.id = linkUniqueId++;
                        link.from = previousPassNode->outputs[0].id;
                        link.to = pin.id;
                        link.rdgEdge = nullptr;
                        links.push_back(link);
                        pin.pinLinks.push_back(&links.back());
                    }
                    node.inputs.push_back(pin);

                    std::set<GraphNode*> inputResources;
                    for(auto& inEdge : passNode->InEdges<RDGEdge>())
                    {
                        GraphNode* resourceNode = rdgNodeIdToNodes[inEdge->From()->ID()];
                                
                        if(inputResources.contains(resourceNode)) continue;
                        inputResources.insert(resourceNode);

                        GraphPin pin = {};
                        pin.id = pinUniqueId++;
                        pin.node = &node;
                        pin.kind = ed::PinKind::Input;
                        pin.type = PinType::InResource;

                        GraphLink link = {};
                        link.id = linkUniqueId++;
                        link.from = resourceNode->outputs[0].id;
                        link.to = pin.id;
                        link.rdgEdge = inEdge;
                        links.push_back(link);

                        pin.pinLinks.push_back(&links.back());
                        resourceNode->outputs[0].pinLinks.push_back(&links.back());
                        node.inputs.push_back(pin);
                    }           

                    std::set<GraphNode*> outputResources;
                    for(auto& outEdge : passNode->OutEdges<RDGEdge>())
                    {
                        GraphNode* resourceNode = rdgNodeIdToNodes[outEdge->To()->ID()];
                                
                        if(outputResources.contains(resourceNode)) continue;
                        outputResources.insert(resourceNode);

                        GraphPin pin = {};
                        pin.id = pinUniqueId++;
                        pin.node = &node;
                        pin.kind = ed::PinKind::Output;
                        pin.type = PinType::OutResource;

                        GraphLink link = {};
                        link.id = linkUniqueId++;
                        link.from = pin.id;
                        link.to = resourceNode->inputs[0].id;
                        link.rdgEdge = outEdge;
                        links.push_back(link);

                        pin.pinLinks.push_back(&links.back());
                        resourceNode->inputs[0].pinLinks.push_back(&links.back());
                        node.outputs.push_back(pin);
                    }   

                    passNodes.push_back(node);
                    passNodesMap[name] = &passNodes.back(); // vector的地址会变，需要预分配尺寸

                    previousPassNode = &passNodes.back();
                } 
                rdgNodeIdToNodes[passNode->ID()] = passNodesMap[name];
            }
        }


        ed::SetCurrentEditor(context);
        ed::Begin("My Editor", ImVec2(0.0, 0.0f));
        {
            // 绘制资源节点
            for(auto& node : resourceNodes)
            {
                ed::BeginNode(node.id);
                PinUI(node.inputs[0], true);
                ImGui::SameLine();
                ImGui::Text("%s", node.name.c_str());
                ImGui::SameLine();
                PinUI(node.outputs[0], true);
                ed::EndNode();

                if(init)
                {
                    ed::SetNodePosition(node.id, nodePosition);
                    nodePosition += ImVec2(ImGui::GetItemRectMax().x - ImGui::GetItemRectMin().x + 50, 0);
                    if(nodePosition.x > 1500) nodePosition = ImVec2(0, nodePosition.y + 50);
                }
            }

            // 绘制pass结点
            nodePosition = ImVec2(0, nodePosition.y + 100);
            for(auto& node : passNodes)
            {
                ImVec2 headerMin;
                ImVec2 headerMax;
                ed::BeginNode(node.id);
                {
                    ImGui::Text("%s", node.name.c_str());
                    headerMin = ImGui::GetItemRectMin();
                    headerMax = ImGui::GetItemRectMax();
                    ImGui::Dummy(ImVec2(0, 4));

                    float halfBorderWidth = ed::GetStyle().NodeBorderWidth * 0.5f;
                    bool nodeSelected = ed::IsNodeSelected(node.id);

                    {
                        // 输入
                        ImGui::BeginGroup();
                        for(auto& input : node.inputs)
                        {
                            bool selected = nodeSelected || input.type == PinType::Flow;
                            PinUI(input, selected);

                            if(selected)
                            {
                                for(auto& link : input.pinLinks)    LinkUI(*link);     
                            }
                        }
                        ImGui::EndGroup();

                        ImVec2 currentPos = ImGui::GetItemRectMax();
                        if(currentPos.x + PIN_ICON_SIZE + ImGui::GetStyle().ItemSpacing.x * 2 < headerMax.x)
                        {
                            ImGui::SameLine();
                            ImGui::Dummy(ImVec2(headerMax.x - currentPos.x - PIN_ICON_SIZE - ImGui::GetStyle().ItemSpacing.x * 2, 0));
                        }
                        ImGui::SameLine();

                        // 输出
                        ImGui::BeginGroup();
                        for(auto& output : node.outputs)
                        {
                            bool selected = nodeSelected || output.type == PinType::Flow;
                            PinUI(output, selected);

                            if(selected)
                            {
                                for(auto& link : output.pinLinks)  LinkUI(*link);
                            }
                        }
                        ImGui::EndGroup();
                        headerMax.x = std::max(headerMax.x, ImGui::GetItemRectMax().x);
                    }
                }
                ed::EndNode();

                // 给结点的名称加上背景颜色
                ImU32 hearderColor;
                switch (node.type) {
                case NodeType::RenderPass:      hearderColor = IM_COL32(50, 50, 50, 255); break;
                case NodeType::ComputePass:     hearderColor = IM_COL32(150, 50, 50, 255); break;
                case NodeType::RayTracingPass:  hearderColor = IM_COL32(50, 150, 50, 255); break;
                case NodeType::PresentPass:     hearderColor = IM_COL32(50, 50, 150, 255); break;
                case NodeType::CopyPass:        hearderColor = IM_COL32(50, 150, 150, 255); break;
                default:  break;
                }
                auto drawList = ed::GetNodeBackgroundDrawList(node.id);
                //ImGui::GetWindowDrawList()->AddRectFilled(
                drawList->AddRectFilled(
                    headerMin - ImVec2(7, 7),
                    headerMax + ImVec2(7, 7), hearderColor,
                    ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);   
                
                if(init)
                {
                    ed::SetNodePosition(node.id, nodePosition);
                    nodePosition += ImVec2(ImGui::GetItemRectMax().x - ImGui::GetItemRectMin().x + 50, 0);
                    nodePositionY = std::max(nodePositionY, ed::GetNodeSize(node.id).y); 
                    if(nodePosition.x > 1500) 
                    {
                        nodePosition = ImVec2(0, nodePosition.y + nodePositionY + 50);
                        nodePositionY = 0;
                    }
                }
            }

            // 绘制选中的资源结点的边
            for(auto& node : resourceNodes)
            {
                bool nodeSelected = ed::IsNodeSelected(node.id);
                if(nodeSelected)
                {
                    if(nodeSelected)
                    {
                        for(auto& link : node.outputs[0].pinLinks)  LinkUI(*link);
                        for(auto& link : node.inputs[0].pinLinks)   LinkUI(*link);
                    }
                }
            }
        }
        if(init || EngineContext::GetCurretTick() < 5) ed::NavigateToContent();
        
        

        // 绘制鼠标悬浮的信息
        ed::Suspend();

        if (ed::ShowNodeContextMenu(&contextNodeId))
            ImGui::OpenPopup("Link Node Menu");
        else if(ed::ShowLinkContextMenu(&contextLinkId))
            ImGui::OpenPopup("Link Context Menu");


        GraphNode* contextNode = nullptr;
        for(auto& node : resourceNodes) 
        {
            if(node.id == contextNodeId) 
            {
                contextNode = &node;
                break;
            }
        }
        if(contextNode && ImGui::BeginPopup("Link Node Menu"))
        {  
            ResourceNodeHoverUI(*contextNode);
            ImGui::EndPopup();
        }
        
        GraphLink* contextLink = nullptr;
        for(auto& link : links) 
        {
            if(link.id == contextLinkId) 
            {
                contextLink = &link;
                break;
            }
        }
        if (contextLink && ImGui::BeginPopup("Link Context Menu"))
        {
            LinkHoverUI(*contextLink);
            ImGui::EndPopup();
        }
        ed::Resume();




        ed::End();
        ed::SetCurrentEditor(nullptr);
    }
    ImGui::End();

    // ed::DestroyEditor(context);
}