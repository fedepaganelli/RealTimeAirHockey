#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <iostream>
#include <cstring>
#include <pthread.h>
#include "config.h"
#include "gamestate.h"
#include "physics.h"

GameState g_state;
pthread_mutex_t g_state_mutex;

int main(int argc, char* argv[]) {
    
    g_state.puck_pos = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
    g_state.puck_vel = { 400.0f, 300.0f }; 
    g_state.player_pos = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT - 100.0f };
    g_state.enemy_pos  = { WINDOW_WIDTH / 2.0f, 100.0f };
    g_state.score_player = 0;
    g_state.score_enemy = 0;
    g_state.is_running = true;
    g_state.trigger_goal_sound = false;
    g_state.trigger_hit_sound = false;
    g_state.phase = GamePhase::MENU;
    g_state.winner = 0;

    //the mutex uses PIP
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&g_state_mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "error during SDL2 initialization: " << SDL_GetError() << std::endl;
        return -1;
    }

    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags) {
        std::cerr << "error during IMG_Init: " << IMG_GetError() << std::endl;
        return -1;
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "error during Mix_OpenAudio: " << Mix_GetError() << std::endl;
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow("Real-Time Air Hockey", 
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
        WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);

    if (!window) {
        std::cerr << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    
    if (!renderer) {
        std::cerr << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Texture* tex_bg = IMG_LoadTexture(renderer, "assets/background.jpg");
    SDL_Texture* tex_puck = IMG_LoadTexture(renderer, "assets/puck.png");
    SDL_Texture* tex_player = IMG_LoadTexture(renderer, "assets/bat.png");
    SDL_Texture* tex_enemy = IMG_LoadTexture(renderer, "assets/bat.png");

    Mix_Chunk* sfx_hit = Mix_LoadWAV("assets/hit.wav");
    Mix_Chunk* sfx_goal = Mix_LoadWAV("assets/goal.wav");

    //physics thread with SCHED_FIFO and priority = 90
    pthread_attr_t physics_attr;
    pthread_attr_init(&physics_attr);
    pthread_attr_setinheritsched(&physics_attr, PTHREAD_EXPLICIT_SCHED);
   
    int rc = pthread_attr_init(&physics_attr);
    if (rc != 0) {
        std::cerr << "pthread_attr_init: " << strerror(rc) << std::endl;
        return -1;
    }
    
    rc = pthread_attr_setinheritsched(&physics_attr, PTHREAD_EXPLICIT_SCHED);
    if (rc != 0)
        std::cerr << "setinheritsched: " << strerror(rc) << std::endl;
    
    rc = pthread_attr_setschedpolicy(&physics_attr, SCHED_FIFO);
    if (rc != 0)
        std::cerr << "setschedpolicy: " << strerror(rc) << std::endl;
    
    struct sched_param param{};
    param.sched_priority = 90;
    
    rc = pthread_attr_setschedparam(&physics_attr, &param);
    if (rc != 0)
        std::cerr << "setschedparam: " << strerror(rc) << std::endl;
    
    pthread_t physics_thread_id;
    rc = pthread_create(&physics_thread_id, &physics_attr, physics_task, NULL);
    
    pthread_attr_destroy(&physics_attr);
    
    if (rc != 0) {
        std::cerr << "pthread_create: " << strerror(rc)
                  << " (error code: " << rc << ")" << std::endl;
        return -1;
    }
    std::cout << "\n press SPACE to start the game!" << std::endl;
    
    SDL_ShowCursor(SDL_DISABLE);
    
    SDL_Event event;
    bool local_running = true;

    while (local_running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                pthread_mutex_lock(&g_state_mutex);
                g_state.is_running = false;
                pthread_mutex_unlock(&g_state_mutex);
                local_running = false;
            }
            
            //FSM transitions
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_SPACE) {
                    pthread_mutex_lock(&g_state_mutex);
                    
                    if (g_state.phase == GamePhase::MENU) {
                        g_state.phase = GamePhase::PLAYING;
                        std::cout << "Game started, let's arrive at " << MAX_SCORE << std::endl;
                    } 
                    else if (g_state.phase == GamePhase::GAME_OVER) {
                        g_state.score_player = 0;
                        g_state.score_enemy = 0;
                        g_state.winner = 0;
                        g_state.puck_pos = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
                        g_state.puck_vel = { 400.0f, 300.0f };
                        g_state.player_pos = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT - 100.0f };
                        g_state.enemy_pos  = { WINDOW_WIDTH / 2.0f, 100.0f };
                        g_state.phase = GamePhase::MENU;
                        std::cout << "press SPACE to start the game!" << std::endl;
                    }
                    pthread_mutex_unlock(&g_state_mutex);
                }
            }
        }
        
        int mouse_x, mouse_y;
        SDL_GetMouseState(&mouse_x, &mouse_y);

        pthread_mutex_lock(&g_state_mutex);
        
        g_state.player_pos.x = static_cast<float>(mouse_x);
        g_state.player_pos.y = static_cast<float>(mouse_y);

        //clamping to not escape the border
        if (g_state.player_pos.x < PAD_RADIUS) g_state.player_pos.x = PAD_RADIUS;
        if (g_state.player_pos.x > WINDOW_WIDTH - PAD_RADIUS) g_state.player_pos.x = WINDOW_WIDTH - PAD_RADIUS;
        if (g_state.player_pos.y < WINDOW_HEIGHT / 2.0f + PAD_RADIUS) g_state.player_pos.y = WINDOW_HEIGHT / 2.0f + PAD_RADIUS;
        if (g_state.player_pos.y > WINDOW_HEIGHT - PAD_RADIUS) g_state.player_pos.y = WINDOW_HEIGHT - PAD_RADIUS;

        //we are making a snapshot of the coordinates 
        Vector2D draw_puck_pos = g_state.puck_pos;
        Vector2D draw_player_pos = g_state.player_pos;
        Vector2D draw_enemy_pos = g_state.enemy_pos;
        GamePhase draw_phase = g_state.phase;
        int draw_winner = g_state.winner;

        bool play_hit = g_state.trigger_hit_sound;
        bool play_goal = g_state.trigger_goal_sound;
        g_state.trigger_hit_sound = false;
        g_state.trigger_goal_sound = false;
        
        pthread_mutex_unlock(&g_state_mutex);

        if (play_hit && sfx_hit) Mix_PlayChannel(-1, sfx_hit, 0);
        if (play_goal && sfx_goal) Mix_PlayChannel(-1, sfx_goal, 0);

        //rendering
        if (draw_phase == GamePhase::MENU) {
            SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255); //gray
        } else if (draw_phase == GamePhase::PLAYING) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); //black
        } else if (draw_phase == GamePhase::GAME_OVER) {
            if (draw_winner == 1) SDL_SetRenderDrawColor(renderer, 184, 134, 11, 255); //gold
            else SDL_SetRenderDrawColor(renderer, 80, 0, 0, 255); //red
        }
        SDL_RenderClear(renderer);

        if (tex_bg) {
            SDL_RenderCopy(renderer, tex_bg, NULL, NULL);
        } else {
            SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
            SDL_RenderClear(renderer);
        }

        //we need to center the texture, fucntion lambda 
        auto DrawTextureCentered = [&](SDL_Texture* tex, float x, float y, float radius) {
            if (!tex) return;
            SDL_Rect dstRect = {
                static_cast<int>(x - radius), 
                static_cast<int>(y - radius), 
                static_cast<int>(radius * 2), 
                static_cast<int>(radius * 2)  
            };
            SDL_RenderCopy(renderer, tex, NULL, &dstRect);
        };

        if (draw_phase != GamePhase::MENU) {
            DrawTextureCentered(tex_player, draw_player_pos.x, draw_player_pos.y, PAD_RADIUS);
            DrawTextureCentered(tex_enemy, draw_enemy_pos.x, draw_enemy_pos.y, PAD_RADIUS);
            DrawTextureCentered(tex_puck, draw_puck_pos.x, draw_puck_pos.y, PUCK_RADIUS);
        }
        
        SDL_Rect goal_top = { WINDOW_WIDTH / 2 - GOAL_WIDTH / 2, 0, GOAL_WIDTH, 10 };
        SDL_SetRenderDrawColor(renderer, 1, 174, 243, 255);
        SDL_RenderFillRect(renderer, &goal_top);

        SDL_Rect goal_bottom = { WINDOW_WIDTH / 2 - GOAL_WIDTH / 2, WINDOW_HEIGHT - 10, GOAL_WIDTH, 10 };
        SDL_SetRenderDrawColor(renderer, 1, 174, 243, 255);
        SDL_RenderFillRect(renderer, &goal_bottom);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    pthread_join(physics_thread_id, NULL);
    
    SDL_DestroyTexture(tex_bg);
    SDL_DestroyTexture(tex_puck);
    SDL_DestroyTexture(tex_player);
    SDL_DestroyTexture(tex_enemy);
    Mix_FreeChunk(sfx_hit);
    Mix_FreeChunk(sfx_goal);
    
    Mix_CloseAudio();
    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    pthread_mutex_destroy(&g_state_mutex);
    
    return 0;
}