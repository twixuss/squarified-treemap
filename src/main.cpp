#define TL_IMPL
#include <tl/main.h>
#include <tl/random.h>
#include <tl/math.h>
#include <tl/opengl.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>

#include <SDL.h>

using namespace tl;

using String = Span<utf8>;

struct TreeNode {
    List<TreeNode> children;
    aabb<v2f> rect = {};

    float weight = 0; // For parent nodes stores a sum of the childrens' weight
    v3f color = {};
};

float total_weight(TreeNode *node) {
    if (node->children.count == 0) {
        return node->weight;
    }

    float result = 0;
    for (auto &child : node->children) {
        result += total_weight(&child);
    }
    return result;
}

// Call this after tree modification before placing
void prepare_tree(TreeNode *node) {
    if (node->children.count == 0) {
        return;
    }

    node->weight = 0;
    for (auto &child : node->children) {
        prepare_tree(&child);
        node->weight += child.weight;
    }
    quick_sort(node->children, [&] (TreeNode const &a, TreeNode const &b) { return a.weight > b.weight; });
}

float max_aspect(aabb<v2f> rect) {
    v2f s = rect.size();
    return max(s.x / s.y, s.y / s.x);
}

struct PlaceOptions {
    float padding = 0;
};

void place(TreeNode *parent_node, aabb<v2f> parent_rect, PlaceOptions options = {}) {
    parent_node->rect = parent_rect;
    parent_node->rect = extend(parent_node->rect, V2f(-options.padding));
    if (parent_node->children.count == 0) {
        return;
    }

    Span<TreeNode> remaining_children = parent_node->children;
    List<TreeNode *> current;
    defer { free(current); };
    
    float outer_weight = parent_node->weight;

    while (remaining_children.count) {
        bool tall = parent_rect.size().x < parent_rect.size().y;

        current.clear();

        auto place_current = [&] {
            float current_weight_sum = 0;
            for (auto child : current) {
                current_weight_sum += child->weight;
            }
    
            float weight_acc = 0;
            float next_min = parent_rect.min.s[!tall];
            for (auto child : current) {
                weight_acc += child->weight;
                child->rect = parent_rect;
                child->rect.min.s[tall] = parent_rect.min.s[tall];
                child->rect.min.s[!tall] = next_min;
                child->rect.max.s[tall] = map(current_weight_sum, 0.0f, outer_weight, parent_rect.min.s[tall], parent_rect.max.s[tall]);
                next_min = map(weight_acc, 0.0f, current_weight_sum, parent_rect.min.s[!tall], parent_rect.max.s[!tall]);
                child->rect.max.s[!tall] = next_min;
            }
        };

        auto get_current_max_aspect = [&] {
            float worst_max_aspect = 1;
            for (auto child : current) {
                worst_max_aspect = max(worst_max_aspect, max_aspect(child->rect));
            }
            return worst_max_aspect;
        };

        float best_max_aspect = infinity<f32>;

        for (auto &child : remaining_children) {
            current.add(&child);

            place_current();

            float current_max_aspect = get_current_max_aspect();
            if (current_max_aspect < best_max_aspect) {
                best_max_aspect = current_max_aspect;
                continue;
            } else {
                current.pop();
                place_current();
                remaining_children.set_begin(&child);
                parent_rect.min.s[tall] = current.back()->rect.max.s[tall];
                outer_weight = 0;
                for (auto child : remaining_children) {
                    outer_weight += child.weight;
                }
                goto continue_outer;
            }
        }

        remaining_children.count = 0;
        break;
    continue_outer:;
    }

    assert(remaining_children.count == 0);

    for (auto &child : parent_node->children) {
        place(&child, child.rect, options);
    }
}







xorshift32 rng = {1245136};
TreeNode random_node() {
    return {
        .weight = next_f32(rng) * 9 + 1,
        .color = hsv_to_rgb(next_f32(rng), 1, 1),
    };
}

void add_random_children(TreeNode *node, int depth) {
    node->color = hsv_to_rgb(next_f32(rng), map<f32,f32>(depth, 0, 2, 1, 0.1f), 1);
    if (depth == 0) {
        node->weight = next_f32(rng) * 9 + 1;
        return;
    }

    int n = next_u32(rng) % 9 + 1;
    node->children.resize(n);
    for (int i = 0; i < n; ++i) {
        add_random_children(&node->children[i], depth - 1);
    }
}

int tabs = 0;
void print_tabs() {
    for (int i = 0; i < tabs; ++i) {
        print("  ");
    }
}
void print_tree(TreeNode *node) {
    print_tabs();
    if (node->children.count) {
        println("\\");
        ++tabs;
        for (int i = 0; i < node->children.count; ++i) {
            print_tree(&node->children[i]);
        }
        --tabs;
    } else {
        println(node->weight);
    }
}

SDL_Window *window;
void draw_tree(TreeNode *node) {
    if (node->children.count == 0) {
        int r = (int)(node->color.x * 255);
        int g = (int)(node->color.y * 255);
        int b = (int)(node->color.z * 255);

        //SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        
        auto re = node->rect;
        //re = extend(re, V2f(-2));

        SDL_Rect rect {
            .x = (int)re.min.x,
            .y = (int)re.min.y,
            .w = (int)re.max.x - (int)re.min.x,
            .h = (int)re.max.y - (int)re.min.y,
        };
        v2f wpos = std::bit_cast<v2f>(ImGui::GetWindowPos()) + std::bit_cast<v2f>(ImGui::GetWindowContentRegionMin());
        ImGui::GetWindowDrawList()->AddRectFilled(std::bit_cast<ImVec2>(wpos + re.min), std::bit_cast<ImVec2>(wpos + re.max), ImGui::GetColorU32(std::bit_cast<ImVec4>(V4f(node->color, 1))));
    } else {
        for (auto child : node->children) {
            draw_tree(&child);
        }
    }
}

s32 tl_main(Span<String> args) {

    TreeNode root = {};

    #if 1
    add_random_children(&root, 3);
    #elif 1
    root.children.add(random_node() withx { it.weight = 1; });
    root.children.add(random_node() withx { it.weight = 2; });
    root.children.add(random_node() withx { it.weight = 3; });
    #else
    root.children.add(random_node() withx { it.weight = 1; });
    root.children.add(random_node() withx { it.weight = 1; });
    root.children.add(random_node() withx { it.weight = 1; });
    root.children[0].children.add(random_node() withx { it.weight = 1; });
    root.children[0].children.add(random_node() withx { it.weight = 1; });
    root.children[0].children.add(random_node() withx { it.weight = 1; });
    root.children[1].children.add(random_node() withx { it.weight = 1; });
    root.children[1].children.add(random_node() withx { it.weight = 1; });
    root.children[1].children.add(random_node() withx { it.weight = 1; });
    root.children[2].children.add(random_node() withx { it.weight = 1; });
    root.children[2].children.add(random_node() withx { it.weight = 1; });
    root.children[2].children.add(random_node() withx { it.weight = 1; });
    #endif

    print_tree(&root);

    prepare_tree(&root);

    PlaceOptions place_options = {
        .padding = 1,
    };

    place(&root, {{}, {800, 600}}, place_options);

    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow("Squarified Tree Map", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init();

    SDL_Event event;

    while (1) {
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                return 0;
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                }
            }
        }

        glClearColor(0.1, 0.1, 0.1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        {
            if (ImGui::Begin("Hello, ImGui!")) {
                auto window_size = std::bit_cast<v2f>(ImGui::GetWindowContentRegionMax()) - std::bit_cast<v2f>(ImGui::GetWindowContentRegionMin());
                static v2f old_window_size = {};

                if (any(window_size != old_window_size)) {
                    place(&root, {{}, window_size}, place_options);
                }

                draw_tree(&root);
            }
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
    }

    return 0;
}