
// MIT License
//
// Copyright (c) 2019 degski
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <cassert>
#include <cmath>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <random>
#include <sax/iostream.hpp> // <iostream> + nl, sp etc. defined...
#include <string>
#include <type_traits>
#include <vector>

#include <SFML/Audio.hpp>
#include <SFML/Extensions.hpp>
#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>

// Pong sounds: http://cs.au.dk/~dsound/DigitalAudio.dir/Greenfoot/Pong.dir/Pong.html

#include <sax/autotimer.hpp>
#include <sax/prng.hpp>
#include <sax/uniform_int_distribution.hpp>

#include "resource.h"
#include "type_traits.hpp"

/*
                        C3-----------------------------------N-----------------------------------C0
                        |                                                                         |
                        |                                                                         |
                        |                                                                         |
                        |                                                                         |
                        |                                                                         |
                        |                                                                         |
                        |                                                                         |
                        |                                                                         |
                        |           0.0 pi / 2.0 pi                                               |
                        W                  |                                                      E
                        |          Q3      |      Q0                                              |
                        |                  |                                                      |
                        |   1.5 pi -------Pos------- 0.5 pi                                       |
                        |                  |                                                      |
                        |          Q2      |      Q1                                              |
                        |                  |                                                      |
                        |                1.0 pi                                                   |
                        |                                                                         |
                        |                                                                         |
                        |                                                                         |
                        C2-----------------------------------S-----------------------------------C1
*/

// Random stuff...

using Rng     = sax::Rng;
using UniDisi = sax::uniform_int_distribution<sf::Int32>;
using UniDisf = std::uniform_real_distribution<float>;
using NorDisf = std::normal_distribution<float>;
using BerDisf = std::bernoulli_distribution;

namespace pong {

bool equal ( const float a_, const float b_ ) noexcept { return std::abs ( b_ - a_ ) < 4.0f * FLT_EPSILON; }
bool not_equal ( const float a_, const float b_ ) noexcept { return std::abs ( b_ - a_ ) >= 4.0f * FLT_EPSILON; }
} // namespace pong

struct Sizes {

    sf::Int32 width, height;

    Sizes ( ) noexcept : width ( 0 ), height ( 0 ) {}
    Sizes ( const sf::Vector2i & s_ ) noexcept : width ( s_.x ), height ( s_.y ) {}
    Sizes ( const sf::Vector2u & s_ ) noexcept : width ( ( sf::Int32 ) s_.x ), height ( ( sf::Int32 ) s_.y ) {}
};

struct Numbers {

    sf::Texture m_numbers_texture;
    sf::Sprite m_numbers_sprite;

    Sizes m_sizes;

    Numbers ( ) {
        sf::loadFromResource ( m_numbers_texture, __NUMBERS_TEXTURE__ );
        m_numbers_sprite.setTexture ( m_numbers_texture );
        m_sizes = m_numbers_texture.getSize ( );
        m_sizes.width /= 10;
    }

    sf::IntRect getRect ( const sf::Int32 number_ ) const noexcept {
        return { number_ * m_sizes.width, 0, m_sizes.width, m_sizes.height };
    }
};

struct Score {

    Numbers m_numbers;

    sf::Int32 m_left, m_right;
    sf::Text m_left_text, m_right_text;
    sf::Point m_left_pos, m_right_pos;
    sf::RenderWindowPtr m_render_window_ptr;
    sf::Font m_numbers_font;

    Score ( ) : m_left ( 0 ), m_right ( 0 ) {}

    void create ( sf::RenderWindowRef rwr_, const sf::FloatBox & m_table_box_ ) noexcept {
        m_render_window_ptr = &rwr_;
        sf::loadFromResource ( m_numbers_font, __NUMBERS_FONT__ );
        const sf::Vector2f p = m_table_box_.getSize ( );
        constexpr float shadow_offset = -5.0f;
        m_left_pos.x = std::round ( m_table_box_.left + 0.4f * p.x + shadow_offset );
        m_left_pos.y = std::round ( m_table_box_.top + 0.05f * p.y );
        create_text ( m_left_text, 0.15f * p.y, m_left_pos );
        update_text ( m_left_text, m_left + shadow_offset );
        m_right_pos.x = std::round ( m_table_box_.left + 0.6f * p.x + shadow_offset );
        m_right_pos.y = m_left_pos.y;
        create_text ( m_right_text, 0.15f * p.y, m_right_pos );
        update_text ( m_right_text, m_right );
    }

    void update ( ) noexcept {
        update_text ( m_left_text, m_left );
        update_text ( m_right_text, m_right );
    }

    void reset ( ) noexcept {
        m_left = 0, m_right = 0;
        update ( );
    }

    bool has_won ( ) const noexcept { return m_left > 10 or m_right > 10; }

    private:
    void create_text ( sf::Text & text_, const float size_, const sf::Point & position_ ) const noexcept {
        text_.setString ( ( char ) ( 0 + 48 ) );
        text_.setCharacterSize ( size_ );
        text_.setFont ( m_numbers_font );
        text_.setStyle ( sf::Text::Regular );
        text_.setFillColor ( sf::Color ( 0xCB, 0xCB, 0xCB ) );
        sf::centreOrigin ( text_ );
        text_.setPosition ( position_ );
    }

    void update_text ( sf::Text & text_, const sf::Int32 score_ ) const noexcept { text_.setString ( ( char ) ( score_ + 48 ) ); }
};

struct Ball {

    enum class Direction : sf::Int32 { MovesToRight = 0, MovesToLeft = 1 };
    enum class Event : sf::Int32 { None = 0, HitWall = 1, Missed = 2 };

    mutable Rng m_rng;
    sf::SquareShape m_shape;
    float m_angle, m_speed_increment, m_speed;
    sf::Point m_min, m_max;
    Direction m_direction;
    sf::Point m_previous_position;
    sf::RenderWindowPtr m_render_window_ptr;

    float m_pause;

    Ball ( const float size_ ) :
        m_rng ( sax::os_seed ( ) ),
        m_shape ( sf::makeOdd ( size_ ) ), m_angle ( UniDisf ( 0.333f * sf::pi, 0.666f * sf::pi ) ( m_rng ) ),
        m_speed_increment ( 60.0f / ( float ) sf::getScreenRefreshRate ( ) ), m_speed ( 10.0f * m_speed_increment ),
        m_direction ( ( Direction ) ( m_angle / sf::pi ) ), m_pause ( 0.0f ) {}

    void create ( sf::RenderWindowRef rwr_, const sf::FloatBox & m_table_box_ ) noexcept {
        m_render_window_ptr = &rwr_;
        const float half_ball_size = 0.5 * m_shape.getSize ( ).x;
        m_min = { m_table_box_.left + half_ball_size, m_table_box_.top + half_ball_size },
        m_max = { m_table_box_.right - half_ball_size, m_table_box_.bottom - half_ball_size };
        m_shape.setFillColor ( sf::Color ( 0xE1, 0xE1, 0xE1 ) );
        sf::centreOrigin ( m_shape );
        m_shape.setPosition ( UniDisf ( m_min.x, m_max.x ) ( m_rng ), UniDisf ( m_min.y, m_max.y ) ( m_rng ) );
    }

    void new_ball ( sf::Point & position_ ) noexcept {
        const bool coin_toss = BerDisf ( ) ( m_rng );
        if ( m_direction == Direction::MovesToLeft ) {
            m_angle = coin_toss ? UniDisf ( 1.22f * sf::pi, 1.33f * sf::pi ) ( m_rng )
                                : UniDisf ( 1.66f * sf::pi, 1.78f * sf::pi ) ( m_rng );
        }
        else {
            m_angle = coin_toss ? UniDisf ( 0.66f * sf::pi, 0.78f * sf::pi ) ( m_rng )
                                : UniDisf ( 0.22f * sf::pi, 0.33f * sf::pi ) ( m_rng );
        }
        position_ = { ( m_max.x - m_min.x ) * 0.5f + m_min.x,
                      ( m_max.y - m_min.y ) * ( 0.1f + ( float ) coin_toss * 0.8f ) + m_min.y };
        m_speed = 10.0f;
    }

    void pause ( const float microseconds_ ) noexcept { m_pause = microseconds_; }

    Event update ( Score & score_ ) noexcept {
        static const float inc = 1'000'000.0f / sf::getScreenRefreshRate ( );
        if ( 0.0f < m_pause ) {
            m_pause -= inc;
            return Event::None;
        }
        else {
            m_pause = 0.0f;
        }
        Event event = Event::None;
        m_previous_position    = m_shape.getPosition ( );
        sf::Point new_position = m_previous_position + m_speed * sf::Force ( std::sin ( m_angle ), std::cos ( m_angle ) );
        if ( new_position.x < m_min.x or new_position.x > m_max.x ) {
            score_.m_right += new_position.x < m_min.x;
            score_.m_left += new_position.x > m_max.x;
            event = Event::Missed;
            new_ball ( new_position );
        }
        if ( new_position.y < m_min.y or new_position.y > m_max.y ) {
            m_angle        = sf::clampRadians ( sf::pi - m_angle + NorDisf ( 0.0f, 0.0125f ) ( m_rng ) );
            m_direction    = ( Direction ) ( m_angle / sf::pi );
            new_position.y = new_position.y < m_min.y ? m_min.y : m_max.y;
            event          = Event::HitWall;
        }
        m_shape.setPosition ( new_position );
        return event;
    }
};


#define PADDLE_MOUSE_RATIO 0.4125f
#define PADDLE_MOUSE_MIN_HEIGHT ( ( PADDLE_MOUSE_RATIO ) *sf::VideoMode::getDesktopMode ( ).height )
#define PADDLE_MOUSE_MAX_HEIGHT ( ( 1.0f - ( PADDLE_MOUSE_RATIO ) ) * sf::VideoMode::getDesktopMode ( ).height )


struct Paddle {
    enum class Side : sf::Int32 { Left = 0, Right = 1 };
    mutable Rng m_rng;
    const float m_mouse_min, m_mouse_max, m_paddle_length;
    float m_paddle_detector_length;
    sf::Vector2f m_paddle_detector_offset;
    const sf::Int32 m_paddle_sectors;
    sf::RectangleShape m_shape;
    sf::RenderWindowPtr m_render_window_ptr;
    float m_min_y, m_max_y, m_ratio_y;
    Side m_side = Side::Left;
    float m_pause;

    Paddle ( const float ball_size_, const float paddle_size_ ) :
        m_rng ( sax::os_seed ( ) ),
        m_mouse_min ( PADDLE_MOUSE_MIN_HEIGHT ), m_mouse_max ( PADDLE_MOUSE_MAX_HEIGHT ),
        m_paddle_length ( sf::makeOdd ( 6.0f * sf::makeOdd ( paddle_size_ ) ) ),
        m_paddle_detector_length ( m_paddle_length + ball_size_ ),
        m_paddle_detector_offset ( 0.5f * ( ball_size_ + paddle_size_ ), -0.5f * ( m_paddle_length + ball_size_ ) ),
        m_paddle_sectors ( 15 ), // Has to be odd.
        m_shape ( sf::Vector2f{ sf::makeOdd ( paddle_size_ ), m_paddle_length } ),
        m_pause ( 0.0f ) {
        assert ( m_paddle_sectors & 1 );
    }

    void create ( sf::RenderWindowRef rwr_, const sf::FloatBox & m_table_box_, const Side side_ ) noexcept {
        if ( Side::Right == side_ ) {
            m_paddle_detector_offset.x *= -1.0f;
        }
        m_render_window_ptr = &rwr_;
        m_shape.setFillColor ( sf::Color ( 0xCB, 0xCB, 0xCB ) );
        sf::centreOrigin ( m_shape );
        constexpr float rim_offset = 61.0f;
        m_shape.setPosition ( Side::Right == side_ ? sf::makeOdd ( m_table_box_.right - rim_offset, true )
                                                   : sf::makeOdd ( m_table_box_.left + rim_offset, false ),
                              rwr_.getSize ( ).y / 2.0f );
        m_min_y = m_table_box_.top + 0.075f * ( m_table_box_.bottom - m_table_box_.top );
        m_max_y = m_table_box_.bottom - 0.075f * ( m_table_box_.bottom - m_table_box_.top );
        m_ratio_y = ( 0.85f * ( m_table_box_.bottom - m_table_box_.top ) ) / ( m_mouse_max - m_mouse_min );
        m_side = side_;
    }

    bool update_player ( Ball & ball_ ) noexcept {
        static const float inc = 1'000'000.0f / sf::getScreenRefreshRate ( );
        if ( 0.0f < m_pause ) {
            m_pause -= inc;
            return false;
        }
        else {
            m_pause = 0.0f;
        }
        const float mouse_y =
            ( float ) ( sf::Mouse::getPosition ( *m_render_window_ptr ).y + sf::getWindowTop ( *m_render_window_ptr ) );
        sf::Point ball_position ( ball_.m_shape.getPosition ( ) );
        sf::Point paddle_position ( m_shape.getPosition ( ).x,
                                    m_min_y + m_ratio_y * ( std::clamp ( mouse_y, m_mouse_min, m_mouse_max ) - m_mouse_min ) );
        return update ( ball_, ball_position, paddle_position );
    }

    bool is_y_in_paddle ( const float paddle_centre_y_, const float y_ ) const noexcept {
        // Does the value of y fall into the range of the paddle?
        return y_ > ( paddle_centre_y_ - 0.4f * m_paddle_length ) and y_ < ( paddle_centre_y_ + 0.4f * m_paddle_length );
    }

    bool update_computer ( Ball & ball_ ) noexcept {
        static const float inc = 1'000'000.0f / sf::getScreenRefreshRate ( );
        if ( 0.0f < m_pause ) {
            m_pause -= inc;
            return false;
        }
        else {
            m_pause = 0.0f;
        }
        sf::Point ball_position (
            ball_.m_shape.getPosition ( ) ); // +UniDisf ( 0.5f * -7.0f / 15.0f, 0.5f * 7.0f / 15.0f ) ( m_rng ) * m_paddle_length;
        sf::Point paddle_position ( m_shape.getPosition ( ) );
        if ( not( is_y_in_paddle ( paddle_position.y, ball_position.y ) ) ) {
            if ( ( ( Side::Left == m_side ? Ball::Direction::MovesToLeft == ball_.m_direction
                                          : Ball::Direction::MovesToRight == ball_.m_direction )
                       ? ball_position.y
                       : ( m_min_y + m_max_y ) / 2.0f ) < paddle_position.y ) {
                const float new_paddle_position_y =
                    sf::makeOdd ( paddle_position.y - 9.0f + 9.0f * UniDisf ( -7.0f / 15.0f, 7.0f / 15.0f ) ( m_rng ), false );
                if ( new_paddle_position_y > m_min_y ) {
                    if ( ball_position.y < new_paddle_position_y ) {
                        paddle_position.y = new_paddle_position_y;
                    }
                }
            }
            else {
                const float new_paddle_position_y =
                    sf::makeOdd ( paddle_position.y + 9.0f + 9.0f * UniDisf ( -7.0f / 15.0f, 7.0f / 15.0f ) ( m_rng ), true );
                if ( new_paddle_position_y < m_max_y ) {
                    if ( ball_position.y > new_paddle_position_y ) {
                        paddle_position.y = new_paddle_position_y;
                    }
                }
            }
        }
        return update ( ball_, ball_position, paddle_position );
    }

    void pause ( const float microseconds_ ) noexcept { m_pause = microseconds_; }

    private:
    bool update ( Ball & ball_, sf::Point & ball_position_, sf::Point & paddle_position_ ) noexcept {
        m_shape.setPosition ( paddle_position_ );
        paddle_position_ += m_paddle_detector_offset;

        // Update and return true iff paddle hits the ball.

        // const sf::Point ball_current_position = ball_.m_shape.getPosition ( ); // Before potentially being returned, i.e. could
        // be behind the paddle....

        // Weed out all the positions that are guaranteed to hit the paddle.

        if ( Side::Left == m_side ) {
            if ( Ball::Direction::MovesToRight == ball_.m_direction or
                 ( ball_position_.x > paddle_position_.x or ball_.m_previous_position.x < paddle_position_.x ) ) {
                return false;
            }
        }
        else {
            if ( Ball::Direction::MovesToLeft == ball_.m_direction or
                 ( ball_position_.x < paddle_position_.x or ball_.m_previous_position.x > paddle_position_.x ) ) {
                return false;
            }
        }

        // Could have intersect...

        sf::Point intersection = ball_position_ - ball_.m_previous_position;

        if ( pong::not_equal ( 0.0f, intersection.x ) ) { // Not vertical (slope (s) is inf).
            const float s = intersection.y / intersection.x;
            // y = s * x + b
            intersection.y = s * ( paddle_position_.x - ball_position_.x ) + ball_position_.y;
            if ( intersection.y >= paddle_position_.y and intersection.y <= ( paddle_position_.y + m_paddle_detector_length ) ) {
                intersection.x = paddle_position_.x;
                return_ball ( ball_, intersection,
                              ( ball_position_.x - ball_.m_previous_position.x ) /
                                  ( ball_position_.x - ball_.m_previous_position.x ) );
                return true;
            }
            return false;
        }

        else { // Vertical: detector and ball trajectory are colinear with overlap.
            intersection = paddle_position_; // Select top of detector (assume ball comes from top).
            if ( ball_.m_previous_position.y > paddle_position_.y ) {
                // If the ball comes from below, switch to the bottom of the detector.
                intersection.y += m_paddle_detector_length;
            }
            return_ball ( ball_, intersection,
                          1.0f - ( intersection.x - ball_.m_previous_position.x ) /
                                     ( ball_position_.x - ball_.m_previous_position.x ) );
            return true;
        }
    }

    void return_ball ( Ball & ball_, const sf::Point & intersection_, const float ratio_ ) const noexcept {
        constexpr float epsilon = 0.01f * sf::pi;
        const float zero_pi_or_one_pi = ( float ) ( Ball::Direction::MovesToRight == ball_.m_direction ) * sf::pi;
        float angle                   = sf::half_pi + zero_pi_or_one_pi;
        angle += 0.075f * ( Side::Right == m_side ? sector_hit ( ball_ ) : -sector_hit ( ball_ ) );
        angle += NorDisf ( 0.0f, 0.025f ) ( ball_.m_rng );
        ball_.m_angle     = std::clamp ( angle, zero_pi_or_one_pi + epsilon, sf::pi + zero_pi_or_one_pi - epsilon );
        ball_.m_direction = ( Ball::Direction ) ( ball_.m_angle / sf::pi );
        // Set x-value of the ball so that it won't surpass the paddle on the wrong side.
        ball_.m_speed += ball_.m_speed_increment;
        ball_.m_shape.setPosition ( intersection_ +
                                    ball_.m_speed * ratio_ * sf::Force ( std::sin ( ball_.m_angle ), std::cos ( ball_.m_angle ) ) );
    }

    float sector_hit ( Ball & ball_ ) const noexcept {
        return ( float ) ( std::clamp ( ( ball_.m_shape.getPosition ( ).y - m_shape.getGlobalBounds ( ).top ) / m_paddle_length,
                                        0.0f, 0.999f ) *
                               m_paddle_sectors -
                           m_paddle_sectors / 2u );
    }
};

struct App {

    // Generator.

    mutable Rng m_rng;

    // Draw stuff.

    sf::ContextSettings m_context_settings;
    sf::RenderWindow m_render_window;
    sf::FloatRect m_render_window_bounds;
    sf::FloatBox m_table_box;

    // Frames.

    sf::Int32 m_frame_rate;
    float m_frame_duration_as_microseconds;

    // Resources.

    sf::SoundBuffer m_hit_wall_soundbuffer, m_hit_paddle_soundbuffer, m_miss_ball_soundbuffer;
    sf::Sound m_hit_wall_sound, m_hit_paddle_sound, m_miss_ball_sound;

    sf::Font m_regular_font, m_bold_font, m_mono_font, m_numbers_font;

    sf::Texture m_rim_texture;
    sf::Sprite m_rim_sprite;

    // Drag related.

    sf::Int32 m_desktop_height;

    sf::Vector2i m_grabbed_offset;
    bool m_is_window_grabbed;

    // The objects on the table.

    Ball m_ball;
    Paddle m_player_paddle;
    Paddle m_computer_paddle;
    Score m_score;

    sf::Event m_event;

    App ( ) :

        m_rng ( sax::os_seed ( ) ), m_is_window_grabbed ( false ), m_ball ( 15.0f ), m_player_paddle ( 15.0f, 11.0f ),
        m_computer_paddle ( 15.0f, 11.0f ) {

        m_context_settings.antialiasingLevel = 8u;

        m_render_window.create ( sf::VideoMode ( 1200u, 900u ), L"", sf::Style::None, m_context_settings );
        m_render_window.setVerticalSyncEnabled ( true );
        m_render_window.requestFocus ( );
        m_render_window.setMouseCursorGrabbed ( true );
        m_render_window.setMouseCursorVisible ( false );
        sf::makeWindowSeeThrough ( m_render_window );

        m_render_window_bounds = sf::FloatRect ( 0.0f, 0.0f, m_render_window.getSize ( ).x, m_render_window.getSize ( ).y );

        constexpr float rim_size = 100.0f, shadow_offset = -5.0f;

        m_table_box = sf::FloatBox ( rim_size + shadow_offset, rim_size + shadow_offset,
                                     ( float ) ( m_render_window.getSize ( ).x - rim_size ) + shadow_offset,
                                     ( float ) ( m_render_window.getSize ( ).y - rim_size ) + shadow_offset );

        m_ball.create ( m_render_window, m_table_box );
        m_player_paddle.create ( m_render_window, m_table_box, Paddle::Side::Right );
        m_computer_paddle.create ( m_render_window, m_table_box, Paddle::Side::Left );
        m_score.create ( m_render_window, m_table_box );

        // Frames.

        m_frame_rate                     = sf::getScreenRefreshRate ( );
        m_frame_duration_as_microseconds = 1'000'000.0f / m_frame_rate;

        // Set icon.

        // set_icon ( );

        // Load sound-buffers.

        sf::loadFromResource ( m_hit_wall_soundbuffer, __HIT_WALL_SOUND__ );
        sf::loadFromResource ( m_hit_paddle_soundbuffer, __HIT_PADDLE_SOUND__ );
        sf::loadFromResource ( m_miss_ball_soundbuffer, __MISS_BALL_SOUND__ );

        // Set sounds.

        m_hit_wall_sound.setBuffer ( m_hit_wall_soundbuffer );
        m_hit_paddle_sound.setBuffer ( m_hit_paddle_soundbuffer );
        m_miss_ball_sound.setBuffer ( m_miss_ball_soundbuffer );

        // Load font.

        sf::loadFromResource ( m_numbers_font, __NUMBERS_FONT__ );

        // Load textures and set sprites.

        sf::loadFromResource ( m_rim_texture, __PONG_RIM__ );
        m_rim_sprite.setTexture ( m_rim_texture );

        m_desktop_height = sf::VideoMode::getDesktopMode ( ).height;

        m_render_window.clear ( sf::Color::Transparent );
        m_render_window.draw ( m_rim_sprite );
        m_render_window.display ( );

        sf::sleepForMilliseconds ( 100 );
    }

    bool is_active ( ) const noexcept { return m_render_window.isOpen ( ); }

    void run ( ) noexcept {
        poll_events ( );
        update_state ( );
        render_objects ( );
    }

    private:
    void set_icon ( ) {
        HICON hicon = LoadIcon ( GetModuleHandle ( NULL ), MAKEINTRESOURCE ( __IDI_ICON1__ ) );
        if ( hicon ) {
            SendMessage ( m_render_window.getSystemHandle ( ), WM_SETICON, ICON_BIG, ( LPARAM ) hicon );
        }
        else {
            std::cout << "Could not load icon." << nl;
        }
    }

    void poll_events ( ) noexcept {
        if ( m_render_window.pollEvent ( m_event ) ) {
            if ( sf::Event::MouseMoved == m_event.type ) {
                if ( m_is_window_grabbed ) {
                    m_render_window.setPosition ( sf::Mouse::getPosition ( ) + m_grabbed_offset );
                }
            }
            else if ( sf::Event::Closed == m_event.type or
                      ( sf::Event::KeyPressed == m_event.type and sf::Keyboard::Escape == m_event.key.code ) ) {

                m_render_window.close ( );
            }
            else if ( sf::Event::MouseButtonPressed == m_event.type ) {
                if ( sf::Mouse::Left == m_event.mouseButton.button ) {
                    m_grabbed_offset    = m_render_window.getPosition ( ) - sf::Mouse::getPosition ( );
                    m_is_window_grabbed = true;
                }
                else if ( sf::Mouse::Right == m_event.mouseButton.button ) {
                }
            }
            else if ( sf::Event::MouseButtonReleased == m_event.type ) {
                if ( sf::Mouse::Left == m_event.mouseButton.button ) {
                    m_is_window_grabbed = false;
                }
                else if ( sf::Mouse::Right == m_event.mouseButton.button ) {
                }
            }
        }
    }

    void update_state ( ) noexcept {

        const Ball::Event event = m_ball.update ( m_score );

        if ( Ball::Event::HitWall == event ) {
            m_hit_wall_sound.play ( );
        }
        else if ( Ball::Event::Missed == event ) {
            m_miss_ball_sound.play ( );
            m_ball.pause ( 500'000.0f );
            if ( Ball::Direction::MovesToLeft == m_ball.m_direction ) {
                m_computer_paddle.pause ( 500'000.0f + 333'333.3f / 2.0f );
            }
        }
        if ( m_player_paddle.update_player ( m_ball ) ) {
            m_hit_paddle_sound.play ( );
            m_computer_paddle.pause ( 333'333.3f );
        }
        if ( m_computer_paddle.update_computer ( m_ball ) ) {
            m_hit_paddle_sound.play ( );
            m_player_paddle.pause ( 333'333.3f );
        }
        m_score.update ( );
    }

    void render_objects ( ) noexcept {
        m_render_window.clear ( sf::Color::Transparent );
        m_render_window.draw ( m_rim_sprite );
        m_render_window.draw ( m_score.m_left_text );
        m_render_window.draw ( m_score.m_right_text );
        m_render_window.draw ( m_ball.m_shape );
        m_render_window.draw ( m_player_paddle.m_shape );
        m_render_window.draw ( m_computer_paddle.m_shape );
        m_render_window.display ( );
    }
};

int main ( ) {
    App app;
    while ( app.is_active ( ) ) {
        app.run ( );
    }
    return EXIT_SUCCESS;
}

/*

float t = 0.0;
float dt = 0.01;

float currentTime = hires_time_in_seconds ( );
float accumulator = 0.0;

State previous;
State current;

while ( !quit ) {
float newTime = time ( );
float frameTime = newTime - currentTime;
if ( frameTime > 0.25 )
frameTime = 0.25;
currentTime = newTime;

accumulator += frameTime;

while ( accumulator >= dt ) {
previousState = currentState;
integrate ( currentState, t, dt );
t += dt;
accumulator -= dt;
}

const float alpha = accumulator / dt;

State state = currentState * alpha +
previousState * ( 1.0 - alpha );

render ( state );
}




#include <SFML/Graphics.hpp>

class Light {
    public:
    Light ( const unsigned int id ) : id ( id ) { }

    sf::Vector2f position;
    sf::Color colour;
    float radius;

    bool operator==( const Light& right ) {
        return id == right.id;
    }

    private:
    unsigned int id;
};

class LightSystem : public sf::Drawable {
    public:
    LightSystem ( const sf::Vector2u& size, const sf::Color& colour, const sf::BlendMode& mode ) : ambient_light ( colour ), mode (
mode ) { lightmap_texture.create ( size.x, size.y ); light_texture.loadFromFile ( "Light.png" ); light_texture.setSmooth ( true );
        light_circle.setTexture ( &light_texture );
    }

    unsigned int add ( const sf::Vector2f& position, const sf::Color& colour, const float radius ) {
        lights.emplace_back ( ++id_counter );

        lights.back ( ).position = position;
        lights.back ( ).colour = colour;
        lights.back ( ).radius = radius;

        return id_counter;
    }

    void remove ( const unsigned int id ) {
        auto it = std::find ( lights.begin ( ), lights.end ( ), id );
        if ( it == lights.end ( ) ) return;
        lights.erase ( it );
    }

    void setPosition ( const unsigned int id, const sf::Vector2f& position ) {
        auto it = std::find ( lights.begin ( ), lights.end ( ), id );
        if ( it == lights.end ( ) ) return;
        it->position = position;
    }

    void setColour ( const unsigned int id, const sf::Color& colour ) {
        auto it = std::find ( lights.begin ( ), lights.end ( ), id );
        if ( it == lights.end ( ) ) return;
        it->colour = colour;
    }

    void setRadius ( const unsigned int id, const float radius ) {
        auto it = std::find ( lights.begin ( ), lights.end ( ), id );
        if ( it == lights.end ( ) ) return;
        it->radius = radius;
    }

    void render ( ) {
        lightmap_texture.clear ( ambient_light );
        for ( auto& light : lights ) {
            light_circle.setRadius ( light.radius );
            light_circle.setOrigin ( light.radius, light.radius );
            light_circle.setPosition ( light.position );
            light_circle.setFillColor ( light.colour );
            lightmap_texture.draw ( light_circle );
        }
        lightmap_texture.display ( );
        lightmap_sprite.setTexture ( lightmap_texture.getTexture ( ) );
    }

    private:
    virtual void draw ( sf::RenderTarget& target, sf::RenderStates states ) const {
        target.draw ( lightmap_sprite, mode );
    }

    private:
    std::vector<Light> lights;
    sf::CircleShape light_circle;
    sf::Texture light_texture;
    sf::Sprite lightmap_sprite;
    sf::RenderTexture lightmap_texture;
    sf::Color ambient_light;
    sf::BlendMode mode;
    unsigned int id_counter;
};

int main ( ) {
    sf::RenderWindow window ( sf::VideoMode ( 640, 480 ), "2D Lighting" );
    window.setFramerateLimit ( 60 );
    sf::Clock clock;
    sf::Time delta;

    sf::Texture world_texture;
    world_texture.loadFromFile ( "World.png" );
    sf::Sprite world ( world_texture );
    sf::View view;
    view.setSize ( static_cast<sf::Vector2f>( window.getSize ( ) ) );

    sf::Texture player_texture;
    player_texture.loadFromFile ( "Player.png" );
    sf::Sprite player ( player_texture );
    player.setOrigin ( static_cast<sf::Vector2f>( player_texture.getSize ( ) ) / 2.0f );
    sf::Vector2f movement;
    float speed = 200;

    // Static lights.
    LightSystem prelights ( window.getSize ( ) * ( unsigned ) 2, sf::Color ( 60, 60, 60 ), sf::BlendMultiply );
    for ( int y = 0; y < 20; y += 2 )
        for ( int x = 0; x < 20; x += 2 )
            prelights.add ( sf::Vector2f ( y * 50.0f, x * 50.0f ), sf::Color::Red, 50.0f );
    prelights.render ( );

    // Dynamic lights.
    LightSystem lights ( window.getSize ( ) * static_cast<unsigned int>( 2 ), sf::Color ( 255, 255, 255 ), sf::BlendMultiply );
    auto light = lights.add ( player.getPosition ( ), sf::Color::Yellow, 75.0f );

    sf::Event event;
    while ( window.isOpen ( ) ) {
        delta = clock.restart ( );

        while ( window.pollEvent ( event ) ) {
            if ( event.type == sf::Event::Closed )
                window.close ( );
            else if ( event.type == sf::Event::KeyPressed ) {
                switch ( event.key.code ) {
                case sf::Keyboard::W:
                    movement.y = -speed;
                    break;
                case sf::Keyboard::S:
                    movement.y = speed;
                    break;
                case sf::Keyboard::A:
                    movement.x = -speed;
                    break;
                case sf::Keyboard::D:
                    movement.x = speed;
                    break;
                case sf::Keyboard::Space:
                    lights.setColour ( light, sf::Color::Cyan );
                    break;
                case sf::Keyboard::F:
                    lights.setColour ( light, sf::Color::Yellow );
                    break;
                }
            }
            else if ( event.type == sf::Event::KeyReleased ) {
                switch ( event.key.code ) {
                case sf::Keyboard::W:
                case sf::Keyboard::S:
                    movement.y = 0.0f;
                    break;
                case sf::Keyboard::A:
                case sf::Keyboard::D:
                    movement.x = 0.0f;
                    break;
                }
            }
        }

        player.move ( movement * delta.asSeconds ( ) );
        lights.setPosition ( light, player.getPosition ( ) );
        view.setCenter ( player.getPosition ( ) );

        window.setView ( view );
        window.clear ( );
        window.draw ( world );
        window.draw ( player );
        window.draw ( prelights );
        lights.render ( );
        window.draw ( lights );
        window.display ( );
    }

    return 0;
}

*/
