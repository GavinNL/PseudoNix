#ifndef SDL_EMSCRIPTEN_APP_H
#define SDL_EMSCRIPTEN_APP_H

#include <SDL.h>
#include <imgui.h>

#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#endif

#if !SDL_VERSION_ATLEAST(2,0,17)
#error This backend requires SDL 2.0.17+ because of SDL_RenderGeometry() function
#endif


struct ImGuiApplication
{
    bool            g_done = false;
    SDL_WindowFlags window_flags = {};//static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window   *  g_window      = {};//SDL_CreateWindow("ImJSchema: Build ImGui Forms with JSON", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920, 1080, window_flags);
    SDL_Renderer *  g_renderer    = {};//SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
};


namespace ImGuiApp
{
template<typename App>
static void em_main_loop(void *_app)
{
    auto * app = static_cast<App*>(_app);

    auto & g_renderer = app->g_renderer;
    auto & g_done = app->g_done;
    auto & g_window = app->g_window;

    //auto & g_renderer = app->g_renderer;

    ImGuiIO& io = ImGui::GetIO();
    // Poll and handle events (inputs, window resize, etc.)
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
    // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
            g_done = true;
        else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(g_window))
            g_done = true;
        else
        {
            // inline check if a template T contains a member function func(int, float)
            // and get its return type
            if constexpr (requires {  App().sdlEvent(event); }) {
                //using return_type = decltype( T().callMe(0, 0.0f));
                //   std::cout << "Contains method callMe(int, float)" << std::endl;
                //   std::cout << " --- The return type is: " << typeid(return_type).name() << std::endl;
                app->sdlEvent(event);
            }
        }
    }

    // and get its return type
    if constexpr (requires {  App().imguiPreFrame(); }) {
        app->imguiPreFrame();
    }

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();


    // inline check if a template T contains a member function func(int, float)
    // and get its return type
    if constexpr (requires {  App().imguiRender(); }) {
        app->imguiRender();
    }
    else
    {
        ImGui::Begin("Default");
        ImGui::TextWrapped("Your App needs to have the void imguiRender() method");
        ImGui::End();
    }

    ImGui::EndFrame();
    // Rendering
    ImGui::Render();

    if constexpr (requires {  App().imguiPostFrame(); }) {
        app->imguiPostFrame();
    }

    if constexpr (requires {  App().sdlRender(); }) {
        app->sdlRender();
    }
    else
    {
        ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
        SDL_RenderSetScale(g_renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(g_renderer, static_cast<Uint8>(clear_color.x * 255), static_cast<Uint8>(clear_color.y * 255), static_cast<Uint8>(clear_color.z * 255), static_cast<Uint8>(clear_color.w * 255));
        SDL_RenderClear(g_renderer);
    }
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_renderer);
    SDL_RenderPresent(g_renderer);


}


template<typename T>
static int run(char const* window_name, uint32_t width, uint32_t height)
{
    T App;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        //printf("Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_WindowFlags window_flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |  SDL_WINDOW_SHOWN);

// From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif
    // Create window with SDL_Renderer graphics context
    App.g_window     = SDL_CreateWindow(window_name,
                                    SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    static_cast<int32_t>(width),
                                    static_cast<int32_t>(height),
                                    window_flags);

    App.g_renderer = SDL_CreateRenderer(App.g_window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (App.g_renderer == nullptr)
    {
        SDL_Log("Error creating SDL_Renderer!");
        return false;
    }


    //
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;


    //------------------
    //io.FontDefault = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/firacode/FiraCode-Medium.ttf", 16);

    //------------------
    if constexpr (requires {  App.imguiInit(); }) {
        App.imguiInit();
    }

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForSDLRenderer(App.g_window, App.g_renderer);
    ImGui_ImplSDLRenderer2_Init(App.g_renderer);
    //

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(em_main_loop<T>,
                                 &App,
                                 0,
                                 true);
#else
    while (!App.g_done)
    {
        em_main_loop<T>(&App);
    }
#endif

    // Cleanup
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(App.g_renderer);
    SDL_DestroyWindow(App.g_window);
    SDL_Quit();

    return 0;
}

}


#endif
