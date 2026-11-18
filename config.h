#pragma once

constexpr int WINDOW_WIDTH = 485;
constexpr int WINDOW_HEIGHT = 840;
constexpr int GOAL_WIDTH = 115;

constexpr float PAD_RADIUS = 30.0f;
constexpr float PUCK_RADIUS = 15.0f;

constexpr float PLAYER_SPEED = 900.0f;
constexpr float ENEMY_SPEED = 650.0f;
constexpr float MAX_PUCK_SPEED = 2000.0f;

constexpr long PERIOD_NS = 10000000;    /* periodo di esecuzione di una attività periodica: 10ms -> 100Hz */

constexpr int MAX_SCORE = 5;