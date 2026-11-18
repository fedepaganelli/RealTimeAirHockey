#pragma once
#include <pthread.h>

enum class GamePhase {
    MENU,
    PLAYING,
    GAME_OVER
};

struct Vector2D {
    float x, y;
};

struct GameState {
    Vector2D puck_pos;
    Vector2D puck_vel;
    
    Vector2D player_pos;
    Vector2D enemy_pos; 

    int score_player;
    int score_enemy;
    bool is_running; 

    bool trigger_hit_sound;
    bool trigger_goal_sound;

    GamePhase phase;
    int winner; // 1 = Giocatore, 2 = IA, 0 = Nessuno
    
};

extern GameState g_state;
extern pthread_mutex_t g_state_mutex;