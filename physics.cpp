#include "physics.h"
#include "gamestate.h"
#include "config.h"
#include <iostream>
#include <cmath>
#include <algorithm>

void add_timespec(struct timespec* ts, long nanoseconds) {
    ts->tv_nsec += nanoseconds;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

void ResolveCircleCollision(Vector2D& puck_pos, Vector2D& puck_vel, 
                            const Vector2D& pad_pos, const Vector2D& pad_vel) {
    
    float dx = puck_pos.x - pad_pos.x;
    float dy = puck_pos.y - pad_pos.y;
    float dist_sq = dx * dx + dy * dy;  //squared euclidean distance, why? no root computation beacuse we can compare this distance with min_dist^2
    
    float min_dist = PUCK_RADIUS + PAD_RADIUS;
    
    if (dist_sq < min_dist * min_dist && dist_sq > 0.0001f) {   //the second check it's used to avoid division by number near to 0
        float dist = std::sqrt(dist_sq);
        
        //the normal component estabilishes the impact
        float nx = dx / dist;
        float ny = dy / dist;
        
        //if 2 circles overlap we need to separate them 
        float overlap = min_dist - dist;
        puck_pos.x += nx * overlap;
        puck_pos.y += ny * overlap;
        
        //relative velocity
        float rel_vx = puck_vel.x - pad_vel.x;
        float rel_vy = puck_vel.y - pad_vel.y;
        float vel_along_normal = rel_vx * nx + rel_vy * ny;
        
        if (vel_along_normal > 0) return;
        
        //simulation of an elastic collision using this math formula (by considering e = 1, which represents the coefficient of restitution, that describes how much a collision is elastic)
        float j = -(1.0f + 1.0f) * vel_along_normal;
        
        puck_vel.x += j * nx;
        puck_vel.y += j * ny;
        
        //added some extra energy to make the game more dinamic
        puck_vel.x += pad_vel.x * 0.4f;
        puck_vel.y += pad_vel.y * 0.4f;

        g_state.trigger_hit_sound = true;
    }
}

void* physics_task(void* arg) {

    struct timespec next_activation;
    clock_gettime(CLOCK_MONOTONIC, &next_activation); //we read the clock time (not the "system" time)
    
    Vector2D last_player_pos = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT - 100.0f };

    int ai_smash_state = 0; 
    int ai_smash_timer = 0; 
    int watchdog_counter = 0;
    
    while (true) {
        pthread_mutex_lock(&g_state_mutex);
        bool running = g_state.is_running;
        GamePhase current_phase = g_state.phase;
        pthread_mutex_unlock(&g_state_mutex);
        
        if (!running) break;

        if (current_phase == GamePhase::PLAYING) {
            
            pthread_mutex_lock(&g_state_mutex);
            
            const int SUB_STEPS = 5;    //every period is 10ms -> every subperiod is 2ms; this enable us to avoid the colliosion tunneling
            float sub_dt = 0.01f / SUB_STEPS; // 0.002 sec per step
            
            Vector2D player_vel = {
                (g_state.player_pos.x - last_player_pos.x) / 0.01f,
                (g_state.player_pos.y - last_player_pos.y) / 0.01f
            };

            Vector2D target_player_pos = g_state.player_pos;
            g_state.player_pos = last_player_pos; 
            
            float pad_step_x = (target_player_pos.x - last_player_pos.x) / SUB_STEPS;   //this is the shift for every interval
            float pad_step_y = (target_player_pos.y - last_player_pos.y) / SUB_STEPS;

            for (int step = 0; step < SUB_STEPS; step++) {
                
                //this enables us to verify the probable collision also during this little shifts
                g_state.player_pos.x += pad_step_x;
                g_state.player_pos.y += pad_step_y;
                
                //Air table drift to bring the puck to the center, a sort of elastic force toward the center
                float center_dx = (WINDOW_WIDTH / 2.0f) - g_state.puck_pos.x;
                float center_dy = (WINDOW_HEIGHT / 2.0f) - g_state.puck_pos.y;
                g_state.puck_vel.x += center_dx * 0.1f * sub_dt;
                g_state.puck_vel.y += center_dy * 0.1f * sub_dt;

                g_state.puck_pos.x += g_state.puck_vel.x * sub_dt;
                g_state.puck_pos.y += g_state.puck_vel.y * sub_dt;
                
                const float FRICTION = 0.999f; //we introduce dynamic friction 
                g_state.puck_vel.x *= FRICTION;
                g_state.puck_vel.y *= FRICTION;


                //little FSM for the opponent movement
                
                //x-alignment of the opponent by looking at the puck position
                if (g_state.enemy_pos.x < g_state.puck_pos.x - 5.0f) g_state.enemy_pos.x += ENEMY_SPEED * sub_dt;
                else if (g_state.enemy_pos.x > g_state.puck_pos.x + 5.0f) g_state.enemy_pos.x -= ENEMY_SPEED * sub_dt;
                
                float current_puck_speed = std::sqrt(g_state.puck_vel.x * g_state.puck_vel.x + g_state.puck_vel.y * g_state.puck_vel.y);
                
                bool in_enemy_corner = (g_state.puck_pos.y < 120.0f) && 
                                       (g_state.puck_pos.x < 120.0f || g_state.puck_pos.x > WINDOW_WIDTH - 120.0f) &&
                                       (current_puck_speed < 80.0f);

                if (ai_smash_state == 0 && in_enemy_corner) {
                    ai_smash_state = 1;  //let's start the loading, called wind-up
                    ai_smash_timer = 60; //step back for 60 sub-steps to load the "kick"
                }
                
                if (ai_smash_state > 0) {
                    ai_smash_timer--;
                    if (ai_smash_timer <= 0) {
                        if (ai_smash_state == 1) {
                            ai_smash_state = 2;   //smashing
                            ai_smash_timer = 40;  //smashing for 40 sub-steps
                        } else {
                            ai_smash_state = 0;   //attack ended, let's come back to the standard beahviour
                        }
                    }
                }

                float current_ai_speed = ENEMY_SPEED;
                float target_x = g_state.puck_pos.x;
                float target_y = g_state.puck_pos.y;

                if (ai_smash_state == 1) {
                    //phase 1: wind-up
                    target_x = WINDOW_WIDTH / 2.0f;
                    target_y = 200.0f; 
                    current_ai_speed = ENEMY_SPEED * 1.5f; 
                } 
                else if (ai_smash_state == 2) {
                    //phase 2: smash
                    target_x = g_state.puck_pos.x;
                    target_y = g_state.puck_pos.y - 10.0f; 
                    if (g_state.puck_pos.x < WINDOW_WIDTH / 2.0f) target_x += 10.0f; else target_x -= 10.0f;
                    
                    current_ai_speed = ENEMY_SPEED * 2.5f; //a little velocity overdrive to have more power during the smash
                } 
                else {
                    //standard beahviour: pursuit and defence
                    if (g_state.puck_pos.y < WINDOW_HEIGHT / 2.0f) {
                        target_x = g_state.puck_pos.x;
                        target_y = g_state.puck_pos.y;
                    } else {
                        target_x = g_state.puck_pos.x;
                        target_y = 100.0f;  //come back to defend the gate
                        current_ai_speed = ENEMY_SPEED * 0.4f;
                    }
                }

                if (g_state.enemy_pos.x < target_x - 3.0f) g_state.enemy_pos.x += current_ai_speed * sub_dt;
                else if (g_state.enemy_pos.x > target_x + 3.0f) g_state.enemy_pos.x -= current_ai_speed * sub_dt;
                
                if (g_state.enemy_pos.y < target_y - 3.0f) g_state.enemy_pos.y += current_ai_speed * sub_dt;
                else if (g_state.enemy_pos.y > target_y + 3.0f) g_state.enemy_pos.y -= current_ai_speed * sub_dt;

                if (g_state.enemy_pos.x < PAD_RADIUS) g_state.enemy_pos.x = PAD_RADIUS;
                if (g_state.enemy_pos.x > WINDOW_WIDTH - PAD_RADIUS) g_state.enemy_pos.x = WINDOW_WIDTH - PAD_RADIUS;
                if (g_state.enemy_pos.y > WINDOW_HEIGHT / 2.0f - PAD_RADIUS) g_state.enemy_pos.y = WINDOW_HEIGHT / 2.0f - PAD_RADIUS;
                if (g_state.enemy_pos.y < PAD_RADIUS) g_state.enemy_pos.y = PAD_RADIUS;

                //bounce off the wall
                if (g_state.puck_pos.x <= PUCK_RADIUS) {
                    g_state.puck_pos.x = PUCK_RADIUS; 
                    g_state.puck_vel.x = -g_state.puck_vel.x;
                    g_state.trigger_hit_sound = true;
                } else if (g_state.puck_pos.x >= WINDOW_WIDTH - PUCK_RADIUS) {
                    g_state.puck_pos.x = WINDOW_WIDTH - PUCK_RADIUS;
                    g_state.puck_vel.x = -g_state.puck_vel.x;
                    g_state.trigger_hit_sound = true;
                }
                
                //here we compute the goal extremes
                float goal_left = (WINDOW_WIDTH / 2.0f) - (GOAL_WIDTH / 2.0f);
                float goal_right = (WINDOW_WIDTH / 2.0f) + (GOAL_WIDTH / 2.0f);
                bool goal_scored = false;

                if (g_state.puck_pos.y <= PUCK_RADIUS) { //my goal (so the puck is inside the enemy gate)
                    if (g_state.puck_pos.x > goal_left && g_state.puck_pos.x < goal_right) {
                        g_state.trigger_goal_sound = true;
                        g_state.score_player++;
                        g_state.puck_pos = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };   //puck position resetting to the center
                        g_state.puck_vel = { 0.0f, 300.0f }; 
                        goal_scored = true;
                        
                        if (g_state.score_player >= MAX_SCORE) {
                            g_state.phase = GamePhase::GAME_OVER;
                            g_state.winner = 1;
                            std::cout << "\nWin! Enemy has lost: " << g_state.score_player << " - " << g_state.score_enemy << std::endl;
                        } else {
                            std::cout << "\nGol! You: " << g_state.score_player << " | Enemy: " << g_state.score_enemy << std::endl;
                        }
                    } else {
                        g_state.puck_pos.y = PUCK_RADIUS;
                        g_state.puck_vel.y = std::abs(g_state.puck_vel.y);
                        g_state.trigger_hit_sound = true;
                    }
                } else if (g_state.puck_pos.y >= WINDOW_HEIGHT - PUCK_RADIUS) { // bottom border (me)
                    if (g_state.puck_pos.x > goal_left && g_state.puck_pos.x < goal_right) {
                        g_state.trigger_goal_sound = true;
                        g_state.score_enemy++;
                        g_state.puck_pos = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
                        g_state.puck_vel = { 0.0f, -300.0f }; 
                        goal_scored = true;
                        
                        if (g_state.score_enemy >= MAX_SCORE) {
                            g_state.phase = GamePhase::GAME_OVER;
                            g_state.winner = 2;
                            std::cout << "\nDefeat! Enemy has defeated you: " << g_state.score_enemy << " - " << g_state.score_player << std::endl;
                        } else {
                            std::cout << "\nGol! You: " << g_state.score_player << " | Enemy: " << g_state.score_enemy << std::endl;
                        }
                    } else {
                        g_state.puck_pos.y = WINDOW_HEIGHT - PUCK_RADIUS;
                        g_state.puck_vel.y = -std::abs(g_state.puck_vel.y); 
                        g_state.trigger_hit_sound = true;
                    }
                }

                if (goal_scored) break; //after the goal i interupt the substepping cycle

                Vector2D enemy_vel = {0.0f, 0.0f};
                
                ResolveCircleCollision(g_state.puck_pos, g_state.puck_vel, g_state.player_pos, player_vel);
                ResolveCircleCollision(g_state.puck_pos, g_state.puck_vel, g_state.enemy_pos, enemy_vel);   
            }

            
            bool in_corner = false;
            const float CORNER_SIZE = 70.0f; //70 pixel for the corner
            if ((g_state.puck_pos.x < CORNER_SIZE || g_state.puck_pos.x > WINDOW_WIDTH - CORNER_SIZE) &&
                (g_state.puck_pos.y < CORNER_SIZE || g_state.puck_pos.y > WINDOW_HEIGHT - CORNER_SIZE)) {
                in_corner = true;
            }

            if (in_corner) {    //puck blocked in a corner
                watchdog_counter++;
                if (watchdog_counter >= 300) { //300 means 300*10ms = 30000 ms = 3 s
                    std::cout << "Puck blocked. Restoring" << std::endl;
                    g_state.puck_pos = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
                    g_state.puck_vel = { (g_state.puck_pos.x > WINDOW_WIDTH/2.0f) ? -200.0f : 200.0f, 
                                         (g_state.puck_pos.y < WINDOW_HEIGHT/2.0f) ? 300.0f : -300.0f };
                    watchdog_counter = 0;
                }
            } else {
                watchdog_counter = 0; //reset the watchdog when the puck is outside the corner
            }
            last_player_pos = target_player_pos;

            float speed_sq = g_state.puck_vel.x * g_state.puck_vel.x + g_state.puck_vel.y * g_state.puck_vel.y;
            if (speed_sq > MAX_PUCK_SPEED * MAX_PUCK_SPEED) {
                float speed = std::sqrt(speed_sq);
                g_state.puck_vel.x = (g_state.puck_vel.x / speed) * MAX_PUCK_SPEED;
                g_state.puck_vel.y = (g_state.puck_vel.y / speed) * MAX_PUCK_SPEED;
            }

            pthread_mutex_unlock(&g_state_mutex);
        }

        //we don't guarantee the deadlines respect
        add_timespec(&next_activation, PERIOD_NS);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_activation, NULL);
    }
    
    return NULL;
}