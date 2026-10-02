#include "Audio.h"

#include <algorithm>
#include <cmath>
#include <random>

#ifdef HAVE_PULSE
#include <pulse/error.h>
#include <pulse/simple.h>
#elif defined(_WIN32)
#include <windows.h>
#include <mmreg.h>
#include <mmsystem.h>
#endif

namespace {

constexpr int Rate = 44100;
constexpr double Tau = 6.283185307179586;

// sinus tabulé (4096 points, interpolation linéaire) : bien plus rapide que std::sin, inaudible ici
// x est une phase en tours (1 = 2π), positive
double sin01(double x)
{
    static const std::vector<float> table = [] {
        std::vector<float> t(4097);
        for (int i = 0; i <= 4096; ++i)
            t[i] = float(std::sin(Tau * i / 4096.0));
        return t;
    }();
    const double f = (x - std::floor(x)) * 4096;
    const int i = int(f);
    return table[i] + (table[i + 1] - table[i]) * (f - i);
}

double midiToHz(double m)
{
    return 440.0 * std::pow(2.0, (m - 69) / 12.0);
}

// ---------------------------------------------------------------------------
// Bruitages : petites notes synthétisées (sinus + harmoniques, enveloppe percussive)
// ---------------------------------------------------------------------------

struct Note {
    double start;  // s
    double freq;   // Hz
    double dur;    // s (déclin)
    double gain;
    double bright; // part d'harmoniques
};

std::vector<float> render(const std::vector<Note> &notes, double length, double noise = 0, double noiseDecay = 0.1)
{
    std::vector<float> out(size_t(length * Rate) * 2, 0.f);
    std::mt19937 gen(7);
    std::uniform_real_distribution<float> uni(-1.f, 1.f);
    for (const Note &n : notes) {
        const size_t s0 = size_t(n.start * Rate);
        const size_t len = size_t((n.dur * 4) * Rate);
        for (size_t i = 0; i < len && (s0 + i) * 2 + 1 < out.size(); ++i) {
            const double t = double(i) / Rate;
            const double attack = std::min(1.0, t / 0.004);
            const double env = attack * std::exp(-t / n.dur);
            const double ph = Tau * n.freq * t;
            const double v = (std::sin(ph) + n.bright * 0.5 * std::sin(2 * ph) + n.bright * 0.25 * std::sin(3 * ph))
                             * env * n.gain;
            out[(s0 + i) * 2] += float(v);
            out[(s0 + i) * 2 + 1] += float(v);
        }
    }
    if (noise > 0) { // souffle (démolition)
        float lp = 0;
        for (size_t i = 0; i * 2 + 1 < out.size(); ++i) {
            const double t = double(i) / Rate;
            lp += 0.08f * (uni(gen) - lp);
            const float v = float(lp * noise * std::exp(-t / noiseDecay));
            out[i * 2] += v;
            out[i * 2 + 1] += v;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Musique d'ambiance générative
// ---------------------------------------------------------------------------

// Ambiance calme : longue nappe qui respire, accords tenus ~7 s, basse en douce
// et quelques notes isolées de piano feutré noyées dans un écho sombre.
class Music
{
public:
    Music()
        : m_gen(std::random_device{}())
    {
        for (auto &d : m_delayL)
            d = 0;
        for (auto &d : m_delayR)
            d = 0;
    }

    // ajoute un bloc stéréo entrelacé (volume déjà appliqué par l'appelant)
    void render(float *out, int frames)
    {
        for (int f = 0; f < frames; ++f) {
            if (m_sampleInStep-- <= 0)
                nextStep();
            double l = 0, r = 0;

            // nappe : sinus + un peu de triangle, légèrement désaccordés, fondu lent entre accords
            m_padMix = std::min(1.0, m_padMix + 1.0 / (Rate * 3.5));
            const double mix = m_padMix * m_padMix * (3 - 2 * m_padMix); // fondu adouci
            m_breath += 0.07 / Rate; // respiration lente (~14 s)
            m_breath -= std::floor(m_breath);
            const double swell = 0.8 + 0.2 * sin01(m_breath);
            const bool fading = mix < 1; // l'accord précédent n'est calculé que pendant le fondu
            for (int v = 0; v < 4; ++v) {
                for (int side = 0; side < 2; ++side) {
                    const double det = side ? 1.0012 : 0.9988;
                    double &ph = m_padPhase[v][side];
                    ph += m_padFreq[v] * det / Rate;
                    ph -= std::floor(ph);
                    auto voice = [](double x) { return 0.75 * sin01(x) + 0.25 * (4 * std::abs(x - 0.5) - 1); };
                    double sv = voice(ph);
                    if (fading) {
                        double &pph = m_prevPhase[v][side];
                        pph += m_prevFreq[v] * det / Rate;
                        pph -= std::floor(pph);
                        sv = sv * mix + voice(pph) * (1 - mix);
                    }
                    (side ? r : l) += sv * 0.04 * swell;
                }
            }
            // passe-bas prononcé : son feutré
            m_lpL += 0.03 * (l - m_lpL);
            m_lpR += 0.03 * (r - m_lpR);
            l = m_lpL;
            r = m_lpR;

            // basse : sinus tenu, monte doucement à chaque accord puis s'efface
            m_bassPhase += m_bassFreq / Rate;
            m_bassPhase -= std::floor(m_bassPhase);
            m_bassEnv += (m_bassTarget - m_bassEnv) * (1.0 / (Rate * 1.2));
            m_bassTarget *= 0.999993;
            const double bass = sin01(m_bassPhase) * m_bassEnv * 0.09;
            l += bass;
            r += bass;

            // notes de piano feutré : attaque douce, long déclin
            double al = 0, ar = 0;
            for (auto &n : m_notes) {
                n.phase += n.freq / Rate;
                n.rise = std::min(1.0, n.rise + 1.0 / (Rate * 0.025));
                n.amp *= n.decay;
                const double s = (sin01(n.phase) + 0.12 * sin01(2 * n.phase)) * n.amp * n.rise;
                al += s * (1 - n.pan);
                ar += s * n.pan;
            }
            m_notes.erase(std::remove_if(m_notes.begin(), m_notes.end(), [](const Voice &n) { return n.amp < 1e-4; }),
                          m_notes.end());
            // écho ping-pong assombri à chaque passage
            const int dIdx = m_delayPos;
            const double echoL = m_delayR[dIdx], echoR = m_delayL[dIdx];
            m_fbL += 0.25 * (echoL - m_fbL);
            m_fbR += 0.25 * (echoR - m_fbR);
            m_delayL[dIdx] = float(al + m_fbL * 0.5);
            m_delayR[dIdx] = float(ar + m_fbR * 0.5);
            m_delayPos = (m_delayPos + 1) % DelayLen;
            l += al + m_fbL * 0.55;
            r += ar + m_fbR * 0.55;

            out[f * 2] += float(l * 1.8); // niveau de fond discret au volume par défaut
            out[f * 2 + 1] += float(r * 1.8);
        }
    }

private:
    static constexpr double Bpm = 60;
    static constexpr int DelayLen = int(Rate * 0.75); // 3 croches à 60 BPM

    struct Voice {
        double freq, phase, amp, decay, pan, rise;
    };

    void nextStep()
    {
        const double stepSec = 60.0 / Bpm / 2.0; // croche
        m_sampleInStep = int(stepSec * Rate);
        const int stepInPhrase = m_step % 16; // un accord toutes les 2 mesures (8 s)
        if (stepInPhrase == 0) {
            // Fmaj7 – Cmaj7/E – Am7 – G(sus) : progression lente et apaisée
            static const int chords[4][4] = {{53, 57, 60, 64}, {52, 55, 59, 60}, {52, 57, 60, 67}, {50, 55, 60, 62}};
            static const int roots[4] = {41, 40, 45, 43};
            const int c = (m_step / 16) % 4;
            for (int v = 0; v < 4; ++v) {
                m_prevFreq[v] = m_padFreq[v];
                m_padFreq[v] = midiToHz(chords[c][v]);
                for (int side = 0; side < 2; ++side)
                    m_prevPhase[v][side] = m_padPhase[v][side];
            }
            m_padMix = 0;
            m_bassFreq = midiToHz(roots[c]);
            m_bassTarget = 1.0;
            m_chord = c;
        }
        // notes rares, sur les temps, plus fréquentes au début de l'accord ; jamais deux de suite
        if (m_step % 2 == 0) {
            const double chance = stepInPhrase < 4 ? 0.45 : 0.22;
            if (!m_lastPlayed && std::uniform_real_distribution<>(0, 1)(m_gen) < chance) {
                static const int chordTones[4][3] = {{69, 72, 76}, {67, 71, 76}, {69, 72, 76}, {67, 74, 79}};
                static const int scale[] = {67, 69, 72, 74, 76, 79};
                const bool useChord = std::uniform_real_distribution<>(0, 1)(m_gen) < 0.7;
                const int note = useChord ? chordTones[m_chord][std::uniform_int_distribution<>(0, 2)(m_gen)]
                                          : scale[std::uniform_int_distribution<>(0, 5)(m_gen)];
                const double pan = std::uniform_real_distribution<>(0.3, 0.7)(m_gen);
                m_notes.push_back({midiToHz(note), 0, 0.04, std::exp(-1.0 / (1.4 * Rate)), pan, 0});
                m_lastPlayed = true;
            } else {
                m_lastPlayed = false;
            }
        }
        ++m_step;
    }

    std::mt19937 m_gen;
    long m_step = 0;
    int m_sampleInStep = 0;
    int m_chord = 0;
    bool m_lastPlayed = false;
    double m_padFreq[4] = {174.6, 220, 261.6, 329.6};
    double m_prevFreq[4] = {174.6, 220, 261.6, 329.6};
    double m_padPhase[4][2] = {};
    double m_prevPhase[4][2] = {};
    double m_padMix = 1;
    double m_breath = 0;
    double m_lpL = 0, m_lpR = 0;
    double m_bassFreq = 87.3, m_bassPhase = 0, m_bassEnv = 0, m_bassTarget = 0;
    std::vector<Voice> m_notes;
    float m_delayL[DelayLen];
    float m_delayR[DelayLen];
    double m_fbL = 0, m_fbR = 0;
    int m_delayPos = 0;
};

} // namespace

// ---------------------------------------------------------------------------

Audio &Audio::instance()
{
    static Audio audio;
    return audio;
}

Audio::Audio()
{
    buildSfx();
}

Audio::~Audio()
{
    stop();
}

void Audio::buildSfx()
{
    const double C5 = midiToHz(72), E5 = midiToHz(76), G5 = midiToHz(79), C6 = midiToHz(84), E6 = midiToHz(88),
                 G6 = midiToHz(91), A4 = midiToHz(69), F4 = midiToHz(65), D4 = midiToHz(62), B5 = midiToHz(83);
    m_sfx.resize(SfxCount);
    m_sfx[Click] = render({{0, midiToHz(96), 0.018, 0.25, 0.2}}, 0.12);
    m_sfx[Station] = render({{0, G5, 0.09, 0.32, 0.4}, {0.07, C6, 0.16, 0.32, 0.4}}, 0.8);
    m_sfx[Connect] = render({{0, E5, 0.08, 0.26, 0.3}, {0.06, B5, 0.14, 0.26, 0.3}}, 0.7);
    // carillon de métro (trois tons)
    m_sfx[NewLine] = render({{0, E5, 0.35, 0.3, 0.5}, {0.18, G5, 0.35, 0.3, 0.5}, {0.36, C6, 0.6, 0.3, 0.5}}, 2.6);
    m_sfx[Demolish] = render({{0, midiToHz(43), 0.18, 0.45, 0.6}, {0.05, midiToHz(38), 0.25, 0.35, 0.3}}, 1.2,
                             0.5, 0.12);
    m_sfx[Coins] = render({{0, E6, 0.06, 0.2, 0.6}, {0.06, G6, 0.06, 0.2, 0.6}, {0.12, midiToHz(96), 0.12, 0.2, 0.6}},
                          0.8);
    m_sfx[Good] = render({{0, C5, 0.2, 0.24, 0.4}, {0.1, E5, 0.2, 0.24, 0.4}, {0.2, G5, 0.2, 0.24, 0.4},
                          {0.3, C6, 0.45, 0.26, 0.4}},
                         2.4);
    m_sfx[Bad] = render({{0, A4, 0.25, 0.28, 0.5}, {0.16, F4, 0.25, 0.28, 0.5}, {0.32, D4, 0.5, 0.3, 0.5}}, 2.4);
    // annonce « information » : carillon cloche à deux notes (partiel inharmonique pour le timbre de cloche)
    const double Bell1 = midiToHz(79), Bell2 = midiToHz(86);
    m_sfx[Notify] = render({{0, Bell1, 0.55, 0.22, 0.2}, {0, Bell1 * 2.76, 0.12, 0.05, 0},
                            {0.22, Bell2, 0.7, 0.22, 0.2}, {0.22, Bell2 * 2.76, 0.14, 0.05, 0}},
                           2.8);
    // « alerte » : quatre notes rapides et claires, pour une décision à prendre
    m_sfx[Alert] = render({{0, midiToHz(81), 0.09, 0.24, 0.8}, {0.12, midiToHz(76), 0.09, 0.24, 0.8},
                           {0.24, midiToHz(81), 0.09, 0.24, 0.8}, {0.36, midiToHz(76), 0.16, 0.24, 0.8}},
                          1.2);
    m_sfx[Error] = render({{0, midiToHz(50), 0.06, 0.3, 0.9}, {0.11, midiToHz(50), 0.08, 0.3, 0.9}}, 0.6);
}

void Audio::play(Sfx sfx)
{
    if (!m_available || !m_sfxOn || m_muted)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.size() < 16)
        m_queue.push_back(sfx);
}

#if defined(HAVE_PULSE) || defined(_WIN32)
#define HAVE_AUDIO_BACKEND
#endif

void Audio::start()
{
#ifdef HAVE_AUDIO_BACKEND
    if (m_running)
        return;
    m_running = true;
    m_thread = std::thread([this] { run(); });
#endif
}

void Audio::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

void Audio::run()
{
#ifdef HAVE_AUDIO_BACKEND
    // mixage commun : musique + bruitages + limiteur doux, en stéréo entrelacée
    Music music;
    std::vector<Voice> voices;
    double musicGain = 0; // fondu à l'activation / coupure
    std::vector<float> mus;
    auto mix = [&](float *buf, int frames) {
        std::fill(buf, buf + frames * 2, 0.f);
        // musique (toujours calculée pour garder son fil ; mixée selon le volume)
        const double target = (m_musicOn && !m_muted) ? m_musicVolume.load() : 0.0;
        if (target > 0 || musicGain > 1e-4) { // musique coupée : plus aucun calcul
            mus.assign(size_t(frames) * 2, 0.f);
            music.render(mus.data(), frames);
            for (int i = 0; i < frames; ++i) {
                musicGain += (target - musicGain) * 0.0005;
                buf[i * 2] += float(mus[i * 2] * musicGain);
                buf[i * 2 + 1] += float(mus[i * 2 + 1] * musicGain);
            }
        } else {
            musicGain = 0;
        }
        // bruitages
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (int s : m_queue)
                voices.push_back({s, 0});
            m_queue.clear();
        }
        const float sfxGain = m_sfxVolume.load();
        for (Voice &v : voices) {
            const std::vector<float> &src = m_sfx[v.sfx];
            for (int i = 0; i < frames * 2 && v.pos < src.size(); ++i)
                buf[i] += src[v.pos++] * sfxGain;
        }
        voices.erase(std::remove_if(voices.begin(), voices.end(),
                                    [this](const Voice &v) { return v.pos >= m_sfx[v.sfx].size(); }),
                     voices.end());
        for (int i = 0; i < frames * 2; ++i)
            buf[i] = float(std::tanh(buf[i]));
    };
    const int frames = 1024;
#endif

#if defined(HAVE_PULSE)
    pa_sample_spec spec;
    spec.format = PA_SAMPLE_FLOAT32LE;
    spec.rate = Rate;
    spec.channels = 2;
    pa_buffer_attr attr;
    attr.maxlength = uint32_t(-1);
    attr.tlength = uint32_t(Rate * 2 * sizeof(float) * 0.06); // ≈ 60 ms de latence
    attr.prebuf = uint32_t(-1);
    attr.minreq = uint32_t(-1);
    attr.fragsize = uint32_t(-1);
    int err = 0;
    pa_simple *pa = pa_simple_new(nullptr, "Metro Builder", PA_STREAM_PLAYBACK, nullptr, "Jeu", &spec, nullptr,
                                  &attr, &err);
    if (!pa) {
        m_available = false;
        m_running = false;
        return;
    }
    m_available = true;
    std::vector<float> buf(frames * 2);
    while (m_running) {
        mix(buf.data(), frames);
        if (pa_simple_write(pa, buf.data(), buf.size() * sizeof(float), &err) < 0)
            break;
    }
    pa_simple_flush(pa, nullptr);
    pa_simple_free(pa);
    m_available = false;
#elif defined(_WIN32)
    // Windows : waveOut (winmm), 4 tampons d'environ 23 ms tournant en file
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = Rate;
    fmt.wBitsPerSample = 32;
    fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEOUT wo = nullptr;
    if (waveOutOpen(&wo, WAVE_MAPPER, &fmt, DWORD_PTR(done), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(done);
        m_available = false;
        m_running = false;
        return;
    }
    m_available = true;
    constexpr int NBuf = 4;
    std::vector<float> bufs[NBuf];
    WAVEHDR hdr[NBuf] = {};
    for (int i = 0; i < NBuf; ++i) {
        bufs[i].assign(frames * 2, 0.f);
        hdr[i].lpData = reinterpret_cast<LPSTR>(bufs[i].data());
        hdr[i].dwBufferLength = DWORD(frames * 2 * sizeof(float));
        waveOutPrepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
        hdr[i].dwFlags |= WHDR_DONE; // libre au départ
    }
    while (m_running) {
        bool wrote = false;
        for (int i = 0; i < NBuf && m_running; ++i) {
            if (!(hdr[i].dwFlags & WHDR_DONE))
                continue;
            mix(bufs[i].data(), frames);
            hdr[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(wo, &hdr[i], sizeof(WAVEHDR));
            wrote = true;
        }
        if (!wrote)
            WaitForSingleObject(done, 50);
    }
    waveOutReset(wo);
    for (int i = 0; i < NBuf; ++i)
        waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
    waveOutClose(wo);
    CloseHandle(done);
    m_available = false;
#endif
}
