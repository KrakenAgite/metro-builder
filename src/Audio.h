#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

// Moteur audio du jeu : musique d'ambiance générée en continu et bruitages synthétisés.
// Tout est calculé en code (aucun fichier son) et joué par PulseAudio / PipeWire
// dans un fil dédié. Sans serveur son, le jeu reste simplement silencieux.
class Audio
{
public:
    enum Sfx { Click, Station, Connect, NewLine, Demolish, Coins, Good, Bad, Error, Notify, Alert, SfxCount };

    static Audio &instance();
    ~Audio();

    void start();
    void stop();
    bool available() const { return m_available; }

    void play(Sfx sfx);
    void setMusicEnabled(bool on) { m_musicOn = on; }
    void setSfxEnabled(bool on) { m_sfxOn = on; }
    void setMusicVolume(float v) { m_musicVolume = v; }
    void setSfxVolume(float v) { m_sfxVolume = v; }
    void setMuted(bool muted) { m_muted = muted; }
    bool musicEnabled() const { return m_musicOn; }
    bool sfxEnabled() const { return m_sfxOn; }
    float musicVolume() const { return m_musicVolume; }
    float sfxVolume() const { return m_sfxVolume; }
    bool muted() const { return m_muted; }

private:
    Audio();
    void run();
    void buildSfx();

    struct Voice {
        int sfx;
        size_t pos;
    };

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_available{false};
    std::atomic<bool> m_musicOn{true}, m_sfxOn{true}, m_muted{false};
    std::atomic<float> m_musicVolume{0.5f}, m_sfxVolume{0.7f};
    std::mutex m_mutex;
    std::vector<int> m_queue;            // bruitages demandés, pris par le fil audio
    std::vector<std::vector<float>> m_sfx; // échantillons stéréo entrelacés
};
