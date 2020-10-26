#include "PlayMode.hpp"

#include "DrawLines.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"
#include "load_save_png.hpp"
#include "read_write_chunk.hpp"
#include "map_generator.hpp"
#include "ColorTextureProgram.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <ctime>
#include <random>
#include <fstream>

PlayMode::PlayMode(Client &client_) : client(client_) {

	srand((unsigned int) time(NULL));

	//----- allocate OpenGL resources -----
	{ //vertex buffer:
		glGenBuffers(1, &vertex_buffer);
		//for now, buffer will be un-filled.

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	{ //vertex array mapping buffer for color_texture_program:
		//ask OpenGL to fill vertex_buffer_for_color_texture_program with the name of an unused vertex array object:
		glGenVertexArrays(1, &vertex_buffer_for_color_texture_program);

		//set vertex_buffer_for_color_texture_program as the current vertex array object:
		glBindVertexArray(vertex_buffer_for_color_texture_program);

		//set vertex_buffer as the source of glVertexAttribPointer() commands:
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

		//set up the vertex array object to describe arrays of BoatMode::Vertex:
		glVertexAttribPointer(
			color_texture_program->Position_vec4, //attribute
			3, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 0 //offset
		);
		glEnableVertexAttribArray(color_texture_program->Position_vec4);
		//[Note that it is okay to bind a vec3 input to a vec4 attribute -- the w component will be filled with 1.0 automatically]

		glVertexAttribPointer(
			color_texture_program->Color_vec4, //attribute
			4, //size
			GL_UNSIGNED_BYTE, //type
			GL_TRUE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 //offset
		);
		glEnableVertexAttribArray(color_texture_program->Color_vec4);

		glVertexAttribPointer(
			color_texture_program->TexCoord_vec2, //attribute
			2, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 + 4*1 //offset
		);
		glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);

		//done referring to vertex_buffer, so unbind it:
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		//done setting up vertex array object, so unbind it:
		glBindVertexArray(0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	// load tileset texture
	{
		std::vector< glm::u8vec4 > data;
		glm::uvec2 size(0, 0);
		load_png(data_path("tileset.png"), &size, &data, UpperLeftOrigin);
		tileset_size = size;

		glGenTextures(1, &tileset_tex);

		glBindTexture(GL_TEXTURE_2D, tileset_tex);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glBindTexture(GL_TEXTURE_2D, 0);

		GL_ERRORS();
	}

	// load level data
	std::ifstream level_file;
	level_file.open(data_path("level_data"));

	std::vector<uint8_t> level;
	std::vector<unsigned int> level_width_vec;

	read_chunk(level_file, "widt", &level_width_vec);
	read_chunk(level_file, "lev0", &level);

	assert(level_width_vec.size() == 1);

	unsigned int level_width = level_width_vec[0];
	assert(level_width != 0);
	unsigned int level_height = ((unsigned int) level.size()) / level_width;

	// generate level objects
	for (unsigned int x = 0; x < level_width; x++) {
		for (unsigned int y = 0; y < level_height; y++) {
			if (level[y * level_width + x] == TILE_WALL) {
				walls.emplace_back(glm::vec2(x, y) * TILE_SIZE, glm::vec2(WALL_SIZE, WALL_SIZE));
			}
			if (level[y * level_width + x] == TILE_INNER) {
				walls.emplace_back(glm::vec2(x, y) * TILE_SIZE, glm::vec2(WALL_SIZE, WALL_SIZE));
				Wall *w = &walls.back();
				w->tile_variant = (rand() % 3) + 1;
			}
			if (level[y * level_width + x] == TILE_SPAWN) {
				spawns.emplace_back(glm::vec2(x, y) * TILE_SIZE);
			}
			if (level[y * level_width + x] == TILE_OUTOFBOUNDS) {
				obwalls.emplace_back(glm::vec2(x, y) * TILE_SIZE, glm::vec2(WALL_SIZE, WALL_SIZE));
			}
		}
	}
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.repeat) {
			//ignore repeats
		} else if (evt.key.keysym.sym == SDLK_LEFT) {
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_RIGHT) {
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_UP) {
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_SPACE) {
			space.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_LEFT) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_RIGHT) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_UP) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_SPACE) {
			space.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {
	if (player != nullptr) {
		//queue data for sending to server:
		//send a seventeen-byte message of type 's':

		client.connections.back().send('s');
		short x_short = (short) player->pos.x;
		short y_short = (short) player->pos.y;
		unsigned char *cx = reinterpret_cast<unsigned char *>(&x_short);
		client.connections.back().send(uint8_t(cx[0]));
		client.connections.back().send(uint8_t(cx[1]));
		unsigned char *cy = reinterpret_cast<unsigned char *>(&y_short);
		client.connections.back().send(uint8_t(cy[0]));
		client.connections.back().send(uint8_t(cy[1]));
		client.connections.back().send(uint8_t(uint8_t(player->airborne) << 2 | uint8_t(player->sliding_left) << 1 | uint8_t(player->sliding_right)));

		auto collision = [](Player p, Wall w) {
			if (p.pos.x >= w.pos.x + w.size.x ||
				p.pos.x <= w.pos.x - p.size.x ||
				p.pos.y >= w.pos.y + w.size.y ||
				p.pos.y <= w.pos.y - p.size.y) return false;
			return true;
		};

		if (left.pressed) player->vel.x -= X_ACCEL * elapsed * elapsed;
		if (right.pressed) player->vel.x += X_ACCEL * elapsed * elapsed;

		if (player->vel.x > MAX_X_SPEED) player->vel.x = MAX_X_SPEED;
		if (player->vel.x < -MAX_X_SPEED) player->vel.x = -MAX_X_SPEED;

		if (!left.pressed && !right.pressed) {
			if (glm::abs(player->vel.x) < MIN_X_SPEED) {
				player->vel.x = 0;
			} else {
				player->vel.x -= glm::sign(player->vel.x) * X_DECEL * elapsed * elapsed;
			}
		}

		if (up.pressed || space.pressed) {
			if (can_jump) {
				player->vel.y = -JUMP_IMPULSE;
				can_jump = false;
			}
			if (player->sliding_left) {
				player->vel.y = -WALL_JUMP_Y_IMPULSE;
				player->vel.x = WALL_JUMP_X_IMPULSE;
				player->sliding_left = false;
			}
			if (player->sliding_right) {
				player->vel.y = -WALL_JUMP_Y_IMPULSE;
				player->vel.x = -WALL_JUMP_X_IMPULSE;
				player->sliding_right = false;
			}
		}
		player->vel.y += GRAVITY * elapsed;
		if ((player->sliding_right || player->sliding_left) && player->vel.y > MAX_SLIDE_SPEED) player->vel.y = MAX_SLIDE_SPEED;

		player->airborne = true;
		player->sliding_left = false;
		player->sliding_right = false;
		can_jump = false;
		player->pos.x += player->vel.x * elapsed;
		for (auto const &wall : walls) {
			if (collision(*player, wall)) {
				if (player->vel.x > 0) {
					player->pos.x = wall.pos.x - player->size.x;
					if (player->vel.y > 0) player->sliding_right = true;
				}
				if (player->vel.x < 0) {
					player->pos.x = wall.pos.x + wall.size.x;
					if (player->vel.y > 0) player->sliding_left = true;
				}
				player->vel.x = 0;
			}
		}

		player->pos.y += player->vel.y * elapsed;
		for (auto const &wall : walls) {
			if (collision(*player, wall)) {
				if (player->vel.y > 0) {
					player->pos.y = wall.pos.y - player->size.y;
					can_jump = true;
					player->airborne = false;
				}
				if (player->vel.y < 0) player->pos.y = wall.pos.y + wall.size.y;
				player->vel.y = 0;
			}
		}

		for (auto const &wall : obwalls) {
			if (collision(*player, wall)) {
				random_spawn();
				client.connections.back().send('p'); // tell server we fell into the pit
				break;
			}
		}

		camera = player->pos + 0.5f * player->size;

		// stop at level edges
		camera.x = glm::max(WINDOW_SIZE.x * 0.5f, camera.x);
		camera.x = glm::min(53.0f * TILE_SIZE - 0.5f * WINDOW_SIZE.x, camera.x);
		camera.y = glm::max(WINDOW_SIZE.y * 0.5f, camera.y);
		camera.y = glm::min(60.0f * TILE_SIZE - 0.5f * WINDOW_SIZE.y, camera.y);
	}

	//send/receive data:
	client.poll([this](Connection *c, Connection::Event event){
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			//std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush();
			//expecting message(s) like 'm' + 3-byte length + length bytes of text:
			while (c->recv_buffer.size() >= 2) {
				//std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush();
				char type = c->recv_buffer[0];
				if (type != 'a') {
					throw std::runtime_error("Server sent unknown message type '" + std::to_string(type) + "'");
				}
				uint8_t size = c->recv_buffer[1];
				if (c->recv_buffer.size() < 3 + 5 * size) break; //if whole message isn't here, can't process
				
				//whole message is here:
				uint8_t color = c->recv_buffer[2];
				bool first_message = false; // whether this is our first update
				if (player == nullptr) {
					first_message = true;
					player = &players[color];
					player->color = color;
				}

				for (uint8_t i = 0; i < MAX_PLAYERS; i++) players[i].exists = false; // auto remove players we don't get updates on
				for (uint8_t i = 0; i < size; i++) {
					unsigned int offset = 3 + i * 5;

					uint8_t color_state = c->recv_buffer[offset];
					uint8_t index = color_state & 0x7;
					Player *p = &players[index];
					p->color = index;
					p->it = (color_state >> 7) & 1;
					p->exists = true;

					auto short_from_buf = [c](const std::vector<char> &buffer, const unsigned int &start_pos) {
						unsigned char s1 = (unsigned char) c->recv_buffer[start_pos+1];
						unsigned char s0 = (unsigned char) c->recv_buffer[start_pos];
						unsigned int data = (s1 << 8 | s0);
						return *reinterpret_cast<short *>(&data);
					};

					glm::vec2 pos = glm::vec2((float) short_from_buf(c->recv_buffer, offset + 1), (float) short_from_buf(c->recv_buffer, offset + 3));
					if (player == p) {
						if (first_message) random_spawn();
					} else {
						p->pos = pos;
						p->airborne = (color_state >> 6) & 1;
						p->sliding_left = (color_state >> 5) & 1;
						p->sliding_right = (color_state >> 4) & 1;
					}
				}

				//and consume this part of the buffer:
				c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 3 + 5 * size);
			}
		}
	}, 0.0);
}

void PlayMode::random_spawn() {
	// pick player spawn
	player->pos = spawns[rand() % spawns.size()];
	player->vel = glm::vec2(0.0f, 0.0f);
}

void PlayMode::drawTexture(std::vector< Vertex > &vertices, glm::vec2 pos, glm::vec2 size, glm::vec2 tilepos, glm::vec2 tilesize, glm::u8vec4 color, float rotation) {

	glm::mat4 rotate_around_origin_mat = glm::mat4(
		glm::vec4(glm::cos(rotation), -glm::sin(rotation), 0.0f, 0.0f),
		glm::vec4(glm::sin(rotation), glm::cos(rotation), 0.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
	);

	glm::vec2 rotation_center_2d = pos + 0.5f * size;

	glm::mat4 translate_to_origin_mat = glm::mat4(
		glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
		glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
		glm::vec4(-rotation_center_2d.x, -rotation_center_2d.y, 0.0f, 1.0f)
	);

	glm::mat4 translate_to_center_mat = glm::mat4(
		glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
		glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
		glm::vec4(rotation_center_2d.x, rotation_center_2d.y, 0.0f, 1.0f)
	);

	glm::mat4 rotate_around_center_mat = translate_to_center_mat * rotate_around_origin_mat * translate_to_origin_mat;

	//inline helper function for textured rectangle drawing:
	auto draw_tex_rectangle = [&vertices, &rotate_around_center_mat](glm::vec2 const &pos, glm::vec2 const &size, glm::vec2 const &tilepos, glm::vec2 const &tilesize, glm::u8vec4 const &color) {
		// bot_left will be top left on screen after clip transformation
		glm::vec4 bot_left = rotate_around_center_mat * glm::vec4(pos.x, pos.y, 0.0f, 1.0f);
		glm::vec4 bot_right = rotate_around_center_mat * glm::vec4(pos.x+size.x, pos.y, 0.0f, 1.0f);
		glm::vec4 top_right = rotate_around_center_mat * glm::vec4(pos.x+size.x, pos.y+size.y, 0.0f, 1.0f);
		glm::vec4 top_left = rotate_around_center_mat * glm::vec4(pos.x, pos.y+size.y, 0.0f, 1.0f);

		//draw rectangle as two CCW-oriented triangles:
		vertices.emplace_back(glm::vec3(bot_left), color, glm::vec2(tilepos.x, tilepos.y));
		vertices.emplace_back(glm::vec3(bot_right), color, glm::vec2(tilepos.x+tilesize.x, tilepos.y));
		vertices.emplace_back(glm::vec3(top_right), color, glm::vec2(tilepos.x+tilesize.x, tilepos.y+tilesize.y));

		vertices.emplace_back(glm::vec3(bot_left), color, glm::vec2(tilepos.x, tilepos.y));
		vertices.emplace_back(glm::vec3(top_right), color, glm::vec2(tilepos.x+tilesize.x, tilepos.y+tilesize.y));
		vertices.emplace_back(glm::vec3(top_left), color, glm::vec2(tilepos.x, tilepos.y+tilesize.y));
	};

	tilepos = glm::vec2(tilepos.x * TILE_SIZE / tileset_size.x, tilepos.y * TILE_SIZE / tileset_size.y);
	tilesize = glm::vec2(tilesize.x * TILE_SIZE / tileset_size.x, tilesize.y * TILE_SIZE / tileset_size.y);
	draw_tex_rectangle(pos, size, tilepos, tilesize, color);
}

void PlayMode::drawBackground(std::vector< Vertex > &vertices) {
	glm::vec2 pos1 = glm::vec2(1.0f, 1.0f) * TILE_SIZE;
	glm::vec2 size1 = glm::vec2(51.0f, 50.0f) * TILE_SIZE;

	glm::vec2 pos2 = glm::vec2(22.0f, 51.0f) * TILE_SIZE;
	glm::vec2 size2 = glm::vec2(9.0f, 39.0f) * TILE_SIZE;

	drawTexture(vertices, pos1 - camera, size1, glm::vec2(2.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::u8vec4(255, 255, 255, 255), 0.0f);
	drawTexture(vertices, pos2 - camera, size2, glm::vec2(2.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::u8vec4(255, 255, 255, 255), 0.0f);
}

void PlayMode::drawWalls(std::vector< Vertex > &vertices) {
	for (auto const &wall : walls) {
		drawTexture(vertices, wall.pos - camera, wall.size, glm::vec2(0.0f, wall.tile_variant), glm::vec2(1.0f, 1.0f), glm::u8vec4(255, 255, 255, 255), 0.0f);
	}
}

void PlayMode::drawPlayers(std::vector< Vertex > &vertices) {
	
	for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
		if (players[i].exists) {
			glm::vec2 tilepos = glm::vec2(1.0f, 0.0f);
			if (players[i].sliding_right) tilepos.y = 1.0f;
			else if (players[i].sliding_left) tilepos.y = 2.0f;
			else if (players[i].airborne) tilepos.y = 3.0f;
			drawTexture(vertices, players[i].pos - camera, players[i].size, tilepos, glm::vec2(1.0f, 1.0f), colors[i], 0.0f);
		}
	}

	for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
		if (players[i].exists && players[i].it) drawTexture(vertices, players[i].pos + glm::vec2(0.0f, -TILE_SIZE) - camera, players[i].size, glm::vec2(1.0f, 4.0f), glm::vec2(1.0f, 1.0f), glm::u8vec4(255, 255, 255, 255), 0.0f);
	}
}

void PlayMode::drawText(std::vector< Vertex > &vertices) {
	auto draw_string = [&](std::string str, glm::vec2 at, glm::u8vec4 color) {
		std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789. ";

		for (int i = 0; i < str.size(); i++) {
			for (int j = 0; j < alphabet.size(); j++) {
				if (str[i] == alphabet[j]) {
					float s = 2.0f;
					drawTexture(vertices, at + (float) i * s * glm::vec2(12.0f, 0.0f), s * glm::vec2(11.0f, 13.0f), glm::vec2(0.0f, 7.0f) + (float) j * glm::vec2(11.0f / 20.0f, 0.0f), glm::vec2(11.0f / 20.0f, 13.0f / 20.0f), color, 0.0f);
				}
			}
		}
	};

	std::string color_names[] = {
		"GREEN",
		"RED",
		"PURPLE",
		"BLUE",
		"YELLOW",
		"BLACK",
		"WHITE",
		"PINK"
	};

	std::string whoisit = "";

	int i = 0;
	for (; i < MAX_PLAYERS; i++) {
		if (players[i].it) {
			whoisit = color_names[i] + " IS IT";
			break;
		}
	}

	float width = whoisit.size() * 12.0f * 2.0f;
	draw_string(whoisit, glm::vec2(-0.5f * width, -0.4f * WINDOW_SIZE.y), colors[i]);
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	std::vector< Vertex > vertices;

	drawBackground(vertices);
	drawWalls(vertices);
	drawPlayers(vertices);
	drawText(vertices);

	glm::mat4 pixels_to_clip = glm::mat4(
		glm::vec4(2.0f / WINDOW_SIZE.x, 0.0f, 0.0f, 0.0f),
		glm::vec4(0.0f, -2.0f / WINDOW_SIZE.y, 0.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
	);

	//use alpha blending:
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//don't use the depth test:
	glDisable(GL_DEPTH_TEST);

	//upload vertices to vertex_buffer:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertices[0]), vertices.data(), GL_STREAM_DRAW); //upload vertices array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//set color_texture_program as current program:
	glUseProgram(color_texture_program->program);

	//upload OBJECT_TO_CLIP to the proper uniform location:
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(pixels_to_clip));

	//use the mapping vertex_buffer_for_color_texture_program to fetch vertex data:
	glBindVertexArray(vertex_buffer_for_color_texture_program);

	//bind the solid white texture to location zero so things will be drawn just with their colors:
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tileset_tex);

	//run the OpenGL pipeline:
	glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));

	//unbind the solid white texture:
	glBindTexture(GL_TEXTURE_2D, 0);

	//reset vertex array to none:
	glBindVertexArray(0);

	//reset current program to none:
	glUseProgram(0);

	GL_ERRORS();
}
