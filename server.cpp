
#include "Connection.hpp"

#include "hex_dump.hpp"

#include <cstring>
#include <chrono>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <unordered_map>

int main(int argc, char **argv) {
#ifdef _WIN32
	//when compiled on windows, unhandled exceptions don't have their message printed, which can make debugging simple issues difficult.
	try {
#endif

	//------------ argument parsing ------------

	if (argc != 2) {
		std::cerr << "Usage:\n\t./server <port>" << std::endl;
		return 1;
	}

	//------------ initialization ------------

	Server server(argv[1]);


	//------------ main loop ------------
	constexpr float ServerTick = 1.0f / 60.0f;

	//server state:
	bool was_touching[8][8];
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 8; j++) was_touching[i][j] = false;
	}
	bool occupiedColors[8];
	for (int i = 0; i < 8; i++) occupiedColors[i] = false;

	//per-client state:
	struct PlayerInfo {
		uint8_t color = 0; // 0-7
		bool it = false;
		short x = 0;
		short y = 0;
		float w = 20.0f;
		float h = 20.0f;
		bool airborne = false;
		bool sliding_left = false;
		bool sliding_right = false;

	};
	std::unordered_map< Connection *, PlayerInfo > players;

	while (true) {
		static auto next_tick = std::chrono::steady_clock::now() + std::chrono::duration< double >(ServerTick);
		//process incoming data from clients until a tick has elapsed:
		while (true) {
			auto now = std::chrono::steady_clock::now();
			double remain = std::chrono::duration< double >(next_tick - now).count();
			if (remain < 0.0) {
				next_tick += std::chrono::duration< double >(ServerTick);
				break;
			}
			server.poll([&](Connection *c, Connection::Event evt){
				if (evt == Connection::OnOpen) {
					//client connected:
					if (players.size() == 8) { // server is full
						c->close();
					} else {
						//create some player info for them:
						PlayerInfo p;
						for (int i = 0; i < 8; i++) {
							if (!occupiedColors[i]) {
								p.color = i;
								occupiedColors[i] = true;
								break;
							}
						}
						players.emplace(c, p);
					}

				} else if (evt == Connection::OnClose) {
					//client disconnected:

					//remove them from the players list:
					auto f = players.find(c);
					occupiedColors[f->second.color] = false;
					assert(f != players.end());
					players.erase(f);


				} else { assert(evt == Connection::OnRecv);
					//got data from client:
					//std::cout << "got bytes:\n" << hex_dump(c->recv_buffer); std::cout.flush();

					//look up in players list:
					auto f = players.find(c);
					assert(f != players.end());
					PlayerInfo &player = f->second;

					//handle messages from client:
					while (c->recv_buffer.size() >= 1) {
						char type = c->recv_buffer[0];
						if (type == 's') { // state message
							if (c->recv_buffer.size() < 6) break;

							auto short_from_buf = [c](const std::vector<char> &buffer, const unsigned int &start_pos) {
								unsigned char s1 = (unsigned char) c->recv_buffer[start_pos+1];
								unsigned char s0 = (unsigned char) c->recv_buffer[start_pos];
								unsigned int data = (s1 << 8 | s0);
								return *reinterpret_cast<short *>(&data);
							};

							player.x = short_from_buf(c->recv_buffer, 1);
							player.y = short_from_buf(c->recv_buffer, 3);
							uint8_t state = c->recv_buffer[5];
							player.airborne = (state >> 2) & 1;
							player.sliding_left = (state >> 1) & 1;
							player.sliding_right = state & 1;

							c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 6);
						} else if (type == 'p') {
							for (auto &[c, other_player] : players) {
								(void)c; //work around "unused variable" warning on whatever version of g++ github actions is running
								other_player.it = false;
							}
							player.it = true;
							c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 1);
						} else {
							std::cout << " unrecognized message received, type " + type << std::endl;
							//shut down client connection:
							c->close();
							return;
						}
					}
				}
			}, remain);
		}

		auto collision = [](PlayerInfo p1, PlayerInfo p2) {
			if (p1.x >= p2.x + p2.w || p1.x + p1.w <= p2.x || p1.y >= p2.y + p2.h || p1.y + p1.h <= p2.y) return false;
			return true;
		};

		//update current game state
		for (auto &[c, player] : players) {
			(void)c; //work around "unused variable" warning on whatever version of g++ github actions is running

			// update collision matrix
			for (auto &[c, other_player] : players) {
				bool touching = collision(player, other_player);

				if ((player.it || other_player.it) && touching && !was_touching[player.color][other_player.color]) {
					player.it = !player.it;
					other_player.it = !other_player.it;
				}

				was_touching[player.color][other_player.color] = touching;
				was_touching[other_player.color][player.color] = touching;
			}
		}
		//std::cout << status_message << std::endl; //DEBUG

		//send updated game state to all clients
		for (auto &[c, player] : players) {
			c->send('a');
			c->send(uint8_t(players.size()));
			c->send(uint8_t(player.color));
			for (auto &[c_other, player_other] : players) {
				(void)c_other;
				c->send(uint8_t(uint8_t(player_other.it) << 7 |
				                uint8_t(player_other.airborne) << 6 |
				                uint8_t(player_other.sliding_left) << 5 |
				                uint8_t(player_other.sliding_right) << 4 | player_other.color));
				unsigned char *cx = reinterpret_cast<unsigned char *>(&player_other.x);
				c->send(uint8_t(cx[0]));
				c->send(uint8_t(cx[1]));
				unsigned char *cy = reinterpret_cast<unsigned char *>(&player_other.y);
				c->send(uint8_t(cy[0]));
				c->send(uint8_t(cy[1]));
			}
		}
	}


	return 0;

#ifdef _WIN32
	} catch (std::exception const &e) {
		std::cerr << "Unhandled exception:\n" << e.what() << std::endl;
		return 1;
	} catch (...) {
		std::cerr << "Unhandled exception (unknown type)." << std::endl;
		throw;
	}
#endif
}
