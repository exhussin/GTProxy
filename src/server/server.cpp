#include <spdlog/spdlog.h>

#include "server.h"
#include "../client/client.h"
#include "../utils/text_parse.h"

namespace server {
    Server::Server(Config* config) : enetwrapper::ENetServer{}, m_config{ config }, m_client{ nullptr }, m_peer{ nullptr }
    {
        m_http = new Http{ config };
        m_http->listen("0.0.0.0", 443);
    }

    Server::~Server()
    {
        delete m_http;
        delete m_peer;
        delete m_client;
    }

    bool Server::start()
    {
        if (!create_host(17000, 1))
            return false;

        start_service();
        return true;
    }

    void Server::on_connect(ENetPeer* peer)
    {
        spdlog::info("New client connected to proxy server!");

        // No need to check if the client is disconnected, because we
        // only accept one peer at a time.

        m_peer = new player::Peer{ peer };

        if (m_client && m_client->is_redirecting()) {
            m_client->redirect();
        }
        else {
            // Request server data using saved headers and params from the growtopia client.
            utils::TextParse text_parse{ m_http->request_server_data() }; // TODO: Handle crash.
            std::string host = text_parse.get("server", 1);
            auto port = text_parse.get<enet_uint16>("port", 1);

            m_client = new client::Client{ m_config, this };
            if (!m_client->start(host, port)) {
                spdlog::error("Failed to connect to client!");

                delete m_client;
                m_client = nullptr;
            }
        }
    }

    void Server::on_receive(ENetPeer* peer, ENetPacket* packet)
    {
        if (!m_client)
            return;

        if (!m_client->get_peer())
            return;

        if (!m_client->get_peer()->is_connected())
            return;

        if (!process_packet(peer, packet))
            return;

        m_client->get_peer()->send_packet_packet(packet);
    }

    void Server::on_disconnect(ENetPeer* peer)
    {
        spdlog::info("Client disconnected from proxy server!");

        if (m_client && !m_client->is_redirecting()) {
            if (m_client->get_peer()->is_connected()) {
                // Yes, useless log.
                spdlog::info("Disconnected from growtopia server!");
            }

            // The client will disconnect now from growtopia server after
            // deleting the client object.
            delete m_client;
            m_client = nullptr;
        }

        delete m_peer;
        m_peer = nullptr;

        if (m_client && m_client->is_redirecting()) {
            spdlog::info("Redirecting to another server!");
        }
    }

    bool Server::process_packet(ENetPeer* peer, ENetPacket* packet)
    {
        player::eNetMessageType message_type{player::message_type_to_string(packet)};
        std::string message_data{ player::get_text(packet) };
        switch (message_type) {
            case player::NET_MESSAGE_GAME_PACKET: {
                player::GameUpdatePacket* game_update_packet{ player::get_struct(packet) };
                return process_tank_update_packet(peer, game_update_packet);
            }
            default:
                break;
        }

        return true;
    }

    bool Server::process_tank_update_packet(ENetPeer* peer, player::GameUpdatePacket* game_update_packet)
    {
        switch (game_update_packet->type) {
            case player::PACKET_DISCONNECT:
                m_peer->disconnect_now();
                on_disconnect(peer);
                break;
            default:
                break;
        }

        return true;
    }
}
