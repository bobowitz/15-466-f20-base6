#include "Mode.hpp"

#include "Connection.hpp"

#include "GL.hpp"
#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct PlayMode : Mode {
	PlayMode(Client &client);
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- textures -----
	GLuint tileset_tex = 0;
	glm::vec2 tileset_size;

	struct Vertex {
		Vertex(glm::vec3 const &Position_, glm::u8vec4 const &Color_, glm::vec2 const &TexCoord_) :
			Position(Position_), Color(Color_), TexCoord(TexCoord_) { }
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};
	static_assert(sizeof(Vertex) == 4*3 + 1*4 + 4*2, "BoatMode::Vertex should be packed");

	GLuint vertex_buffer = 0;
	GLuint vertex_buffer_for_color_texture_program = 0;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t pressed = 0;
	} left, right, up, space;

	//last message from server:
	std::string server_message;

	//connection to server:
	Client &client;

	const glm::uvec2 WINDOW_SIZE = glm::uvec2(640, 640);
	const float TILE_SIZE = 20.0f;
	const float WALL_SIZE = TILE_SIZE;

	struct Wall {
		glm::vec2 pos;
		glm::vec2 size;
		unsigned int tile_variant = 0;
		Wall(glm::vec2 _pos, glm::vec2 _size): pos(_pos), size(_size) { }
	};
	struct Outerwall {
		glm::vec2 pos;
		Outerwall(glm::vec2 _pos): pos(_pos) { }
	};

	std::vector<Wall> walls;
	std::vector<Wall> obwalls;
	std::vector<glm::uvec2> spawns;

	struct Player {
		glm::vec2 pos;
		glm::vec2 size;
		glm::vec2 vel = glm::vec2(0.0f, 0.0f);
		uint8_t color = 0; // equal to index in players[]
		bool it = false;
		bool exists = false;
		bool airborne = false;
		bool sliding_left = false;
		bool sliding_right = false;
		Player() {
			pos = glm::vec2(0.0f, 0.0f);
			size = glm::vec2(20.0f, 20.0f);
		}
	};

	static const uint8_t MAX_PLAYERS = 8;
	const glm::u8vec4 colors[MAX_PLAYERS] = {
		glm::u8vec4(94, 157, 91, 255),
		glm::u8vec4(255, 91, 50, 255),
		glm::u8vec4(179, 44, 255, 255),
		glm::u8vec4(94, 157, 249, 255),
		glm::u8vec4(255, 236, 41, 255),
		glm::u8vec4(66, 72, 69, 255),
		glm::u8vec4(211, 211, 212, 255),
		glm::u8vec4(255, 9, 255, 255)
	};

	Player players[MAX_PLAYERS];
	Player *player = nullptr;

	const float X_DECEL = 50000.0f;
	const float X_ACCEL = 200000.0f;
	const float MAX_X_SPEED = 400.0f;
	const float MIN_X_SPEED = 50.0f;
	const float MAX_SLIDE_SPEED = 200.0f;
	const float JUMP_IMPULSE = 700.0f;
	const float WALL_JUMP_Y_IMPULSE = 700.0f;
	const float WALL_JUMP_X_IMPULSE = 500.0f;
	const float GRAVITY = 1666.0f;
	bool can_jump = false;
	glm::vec2 camera;

	void random_spawn();
	void drawWalls(std::vector< Vertex > &vertices);
	void drawBackground(std::vector< Vertex > &vertices);
	void drawPlayers(std::vector< Vertex > &vertices);
	void drawText(std::vector< Vertex > &vertices);
	void drawTexture(std::vector< Vertex > &vertices, glm::vec2 pos, glm::vec2 size, glm::vec2 tilepos, glm::vec2 tilesize, glm::u8vec4 color, float rotation);
};
