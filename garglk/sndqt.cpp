/******************************************************************************
 *                                                                            *
 * Copyright (C) 2006-2009 by Tor Andersson, Lorenzo Marcantonio.             *
 * Copyright (C) 2010 by Ben Cressey, Chris Spiegel.                          *
 *                                                                            *
 * This file is part of Gargoyle.                                             *
 *                                                                            *
 * Gargoyle is free software; you can redistribute it and/or modify           *
 * it under the terms of the GNU General Public License as published by       *
 * the Free Software Foundation; either version 2 of the License, or          *
 * (at your option) any later version.                                        *
 *                                                                            *
 * Gargoyle is distributed in the hope that it will be useful,                *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with Gargoyle; if not, write to the Free Software                    *
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA *
 *                                                                            *
 *****************************************************************************/

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <unistd.h>

#include <QAudioOutput>
#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QTimer>

#include <libopenmpt/libopenmpt.h>
#include <sndfile.hh>

extern "C" {
#include "glk.h"
#include "garglk.h"

#include "gi_blorb.h"
}

#define giblorb_ID_MOD  (giblorb_make_id('M', 'O', 'D', ' '))
#define giblorb_ID_OGG  (giblorb_make_id('O', 'G', 'G', 'V'))
#define giblorb_ID_FORM (giblorb_make_id('F', 'O', 'R', 'M'))
#define giblorb_ID_AIFF (giblorb_make_id('A', 'I', 'F', 'F'))

/* non-standard types */
#define giblorb_ID_WAVE (giblorb_make_id('W', 'A', 'V', 'E'))

#define GLK_MAXVOLUME 0x10000

struct glk_schannel_struct
{
    glk_schannel_struct(glui32 volume) :
        current_volume(volume),
        target_volume(volume)
    {
    }

    std::unique_ptr<QAudioOutput> audio;

    QTimer timer;
    double duration = 0;
    double difference = 0;
    std::chrono::steady_clock::time_point last_volume_bump;
    long current_volume = 0;
    glui32 target_volume = 0;
    glui32 volume_notify = 0;

    bool paused = false;

    glui32 rock;
    gidispatch_rock_t disprock;
};

static std::set<schanid_t> gli_channellist;

gidispatch_rock_t gli_sound_get_channel_disprock(const channel_t *chan)
{
    return chan->disprock;
}

class SoundSource : public QIODevice {
public:
    SoundSource(int plays) : m_plays(plays) {
    }
    virtual ~SoundSource() { }

    virtual int channels() = 0;
    virtual int rate() = 0;
    virtual qint64 source_read(void *data, qint64 max) = 0;
    virtual void source_rewind() = 0;

    qint64 readData(char *data, qint64 max) override {
        if (m_plays == 0)
            return 0;

        std::size_t n = source_read(data, max);
        if (n == 0 && (m_plays == 0xffffffff || --m_plays > 0))
        {
            source_rewind();
            n = source_read(data, max);
        }

        m_written += n;

        // Ensure at least one full audio buffer was written.
        if (n == 0) {
            qint64 needed = m_buffer_size - m_written;

            if (needed <= 0)
            {
                return 0;
            }
            else
            {
                needed = std::min(needed, max);
                std::memset(data, 0, needed);
                return needed;
            }
        } else {
            return n;
        }
    }

    qint64 writeData(const char *, qint64) override {
        return 0;
    }

    void set_audio_buffer_size(qint64 size)
    {
        m_buffer_size = size;
    }

private:
    glui32 m_plays;
    qint64 m_buffer_size = 0;
    qint64 m_written = 0;
};

// The C++ API requires C++17, so until Gargoyle switches from C++14 to
// C++17, use the C API.
class OpenMPTSource : public SoundSource {
public:
    OpenMPTSource(const QByteArray &buf, int plays) :
        SoundSource(plays),
        m_mod(openmpt_module_create_from_memory2(buf.data(), buf.size(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr), openmpt_module_destroy)
    {
        if (!m_mod)
            throw std::runtime_error("can't parse MOD file");
    }

protected:
    qint64 source_read(void *data, qint64 max) override {
        return 4 * openmpt_module_read_interleaved_stereo(m_mod.get(), rate(), max / 4, reinterpret_cast<int16_t *>(data));
    }

    void source_rewind() override {
        openmpt_module_set_position_seconds(m_mod.get(), 0);
    }

    int channels() override {
        return 2;
    }

    int rate() override {
        return 44100;
    }

private:
    std::unique_ptr<openmpt_module, decltype(&openmpt_module_destroy)> m_mod;
};

class SndfileSource : public SoundSource {
public:
    SndfileSource(const QByteArray &buf, glui32 plays) :
        SoundSource(plays),
        m_buf(buf)
    {
        SF_VIRTUAL_IO io = get_io();
        m_soundfile = SndfileHandle(io, this);
        if (!m_soundfile)
            throw std::runtime_error("can't open with libsndfile");
    }

    qint64 source_read(void *data, qint64 max) override {
        return 2 * m_soundfile.read(reinterpret_cast<short *>(data), max / 2);
    }

    void source_rewind() override {
        m_offset = 0;
        SF_VIRTUAL_IO io = get_io();
        m_soundfile = SndfileHandle(io, this);
    }

    int channels() override {
        return m_soundfile.channels();
    }

    int rate() override {
        return m_soundfile.samplerate();
    }

private:
    SndfileHandle m_soundfile;
    const QByteArray m_buf;
    ssize_t m_offset = 0;

    static SF_VIRTUAL_IO get_io() {
        SF_VIRTUAL_IO io;

        io.get_filelen = vio_get_filelen;
        io.seek = vio_seek;
        io.read = vio_read;
        io.write = vio_write;
        io.tell = vio_tell;

        return io;
    }

#define SOURCE (reinterpret_cast<SndfileSource *>(source))
    static sf_count_t vio_get_filelen(void *source) {
        return SOURCE->m_buf.size();
    }

    static sf_count_t vio_seek(sf_count_t offset, int whence, void *source) {
        ssize_t orig = SOURCE->m_offset;

        switch (whence)
        {
        case SEEK_SET:
            SOURCE->m_offset = offset;
            break;
        case SEEK_CUR:
            SOURCE->m_offset += offset;
            break;
        case SEEK_END:
            SOURCE->m_offset = SOURCE->m_buf.size() + offset;
            break;
        }

        if (SOURCE->m_offset < 0)
        {
            SOURCE->m_offset = orig;
            return -1;
        }

        return SOURCE->m_offset;
    }

    static sf_count_t vio_read(void *ptr, sf_count_t count, void *source) {
        if (SOURCE->m_offset >= SOURCE->m_buf.size())
            return 0;

        if (SOURCE->m_offset + count > SOURCE->m_buf.size())
            count = SOURCE->m_buf.size() - SOURCE->m_offset;

        std::memcpy(ptr, &SOURCE->m_buf.data()[SOURCE->m_offset], count);
        SOURCE->m_offset += count;

        return count;
    }

    static sf_count_t vio_write(const void *, sf_count_t, void *) {
        throw std::runtime_error("writing not supported");
    }

    static sf_count_t vio_tell(void *source) {
        return SOURCE->m_offset;
    }
#undef SOURCE
};

void gli_initialize_sound(void)
{
}

schanid_t glk_schannel_create(glui32 rock)
{
    return glk_schannel_create_ext(rock, GLK_MAXVOLUME);
}

schanid_t glk_schannel_create_ext(glui32 rock, glui32 volume)
{
    channel_t *chan;

    if (!gli_conf_sound)
        return nullptr;

    chan = new channel_t(volume);

    auto on_timeout = [=]() {
        auto now = std::chrono::steady_clock::now();
        double diff = std::chrono::duration<double>(now - chan->last_volume_bump).count();
        chan->last_volume_bump = now;

        chan->current_volume += chan->difference * diff / chan->duration;

        if ((chan->difference > 0 && chan->current_volume >= chan->target_volume) ||
            (chan->difference < 0 && chan->current_volume <= chan->target_volume))
        {
            chan->current_volume = chan->target_volume;
        }

        if (chan->current_volume == chan->target_volume)
        {
            if (chan->volume_notify != 0)
            {
                gli_event_store(evtype_VolumeNotify, nullptr, 0, chan->volume_notify);
                gli_notification_waiting();
            }
            chan->timer.stop();
        }

        if (chan->audio)
            chan->audio->setVolume((double)chan->current_volume / GLK_MAXVOLUME);
    };
    QObject::connect(&chan->timer, &QTimer::timeout, on_timeout);

    gli_channellist.insert(chan);

    if (gli_register_obj)
        chan->disprock = (*gli_register_obj)(chan, gidisp_Class_Schannel);
    else
        chan->disprock.ptr = nullptr;

    return chan;
}

void glk_schannel_destroy(schanid_t chan)
{
    if (chan == nullptr)
    {
        gli_strict_warning("schannel_destroy: invalid id.");
        return;
    }

    glk_schannel_stop(chan);
    if (gli_unregister_obj)
        (*gli_unregister_obj)(chan, gidisp_Class_Schannel, chan->disprock);

    gli_channellist.erase(chan);

    delete chan;
}

schanid_t glk_schannel_iterate(schanid_t chan, glui32 *rock)
{
    std::set<schanid_t>::iterator it;

    if (chan == nullptr)
    {
        it = gli_channellist.begin();
    }
    else
    {
        it = gli_channellist.find(chan);
        if (it != gli_channellist.end())
            ++it;
    }

    if (it == gli_channellist.end())
    {
        if (rock != nullptr)
            *rock = 0;

        return nullptr;
    }
    else
    {
        if (rock != nullptr)
            *rock = (*it)->rock;

        return *it;
    }
}

glui32 glk_schannel_get_rock(schanid_t chan)
{
    if (chan == nullptr)
    {
        gli_strict_warning("schannel_get_rock: invalid id.");
        return 0;
    }
    return chan->rock;
}

glui32 glk_schannel_play(schanid_t chan, glui32 snd)
{
    return glk_schannel_play_ext(chan, snd, 1, 0);
}

glui32 glk_schannel_play_multi(schanid_t *chanarray, glui32 chancount, glui32 *sndarray, glui32 soundcount, glui32 notify)
{
    glui32 successes = 0;

    for (glui32 i = 0; i < chancount; i++)
    {
        successes += glk_schannel_play_ext(chanarray[i], sndarray[i], 1, notify);
    }

    return successes;
}

void glk_sound_load_hint(glui32 snd, glui32 flag)
{
}

void glk_schannel_set_volume(schanid_t chan, glui32 vol)
{
    glk_schannel_set_volume_ext(chan, vol, 0, 0);
}

void glk_schannel_set_volume_ext(schanid_t chan, glui32 glk_volume, glui32 duration, glui32 notify)
{
    if (chan == nullptr)
    {
        gli_strict_warning("schannel_set_volume: invalid id.");
        return;
    }

    if (glk_volume > GLK_MAXVOLUME)
        glk_volume = GLK_MAXVOLUME;

    chan->current_volume = chan->target_volume;
    chan->target_volume = glk_volume;
    chan->volume_notify = notify;

    if (duration == 0)
    {
        chan->current_volume = chan->target_volume;
        if (chan->audio)
            chan->audio->setVolume((double)chan->current_volume / GLK_MAXVOLUME);
    }
    else
    {
        chan->duration = duration / 1000.0;
        chan->difference = (double)chan->target_volume - (double)chan->current_volume;
        chan->timer.start(100);
    }

    chan->last_volume_bump = std::chrono::steady_clock::now();
}

static std::pair<int, QByteArray> load_sound_resource(glui32 snd)
{
    if (!giblorb_is_resource_map())
    {
        QString name = QString("%1/SND%2").arg(gli_workdir).arg(snd);

        QFile file(name);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("can't open SND file");

        QByteArray data = file.readAll();

        struct Magic {
            Magic(qint64 offset, const QString &string) :
                m_offset(offset),
                m_string(string)
            {
            }

            bool matches(QByteArray data) const {
                QByteArray subarray = data.mid(m_offset, m_string.size());

                return QString(subarray) == m_string;
            }

        private:
            const qint64 m_offset;
            const QString m_string;
        };

        std::vector<std::pair<Magic, int>> magics = {
            { Magic(0, "FORM"), giblorb_ID_FORM },
            { Magic(0, "WAVE"), giblorb_ID_WAVE },
            { Magic(0, "RIFF"), giblorb_ID_WAVE },
            { Magic(0, "Extended Module: "), giblorb_ID_MOD },
            { Magic(0, "OggS"), giblorb_ID_OGG },
            { Magic(0x44, "SCRM"), giblorb_ID_MOD },
            { Magic(1080, "4CHN"), giblorb_ID_MOD },
            { Magic(1080, "6CHN"), giblorb_ID_MOD },
            { Magic(1080, "8CHN"), giblorb_ID_MOD },
            { Magic(1080, "16CN"), giblorb_ID_MOD },
            { Magic(1080, "32CN"), giblorb_ID_MOD },
            { Magic(1080, "M.K."), giblorb_ID_MOD },
            { Magic(1080, "M!K!"), giblorb_ID_MOD },
            { Magic(1080, "FLT4"), giblorb_ID_MOD },
            { Magic(1080, "CD81"), giblorb_ID_MOD },
            { Magic(1080, "OKTA"), giblorb_ID_MOD },
            { Magic(1080, "    "), giblorb_ID_MOD },
        };

        for (auto &pair : magics)
        {
            if (pair.first.matches(data))
                return std::make_pair(pair.second, data);
        }

        throw std::runtime_error("no matching magic");
    }
    else
    {
        std::FILE *file;
        glui32 type;
        long pos, len;

        giblorb_get_resource(giblorb_ID_Snd, snd, &file, &pos, &len, &type);
        if (file == nullptr)
            throw std::runtime_error("can't get blorb resource");

        QByteArray data(len, 0);

        if (std::fseek(file, pos, SEEK_SET) == -1 ||
            std::fread(data.data(), len, 1, file) != 1)
        {
            throw std::runtime_error("can't read from blorb file");
        }

        return std::make_pair(type, data);
    }
}

glui32 glk_schannel_play_ext(schanid_t chan, glui32 snd, glui32 repeats, glui32 notify)
{
    if (chan == nullptr)
    {
        gli_strict_warning("schannel_play_ext: invalid id.");
        return 0;
    }

    glk_schannel_stop(chan);

    if (repeats == 0)
        return 1;

    SoundSource *source;
    try
    {
        int type;
        QByteArray data;
        std::tie(type, data) = load_sound_resource(snd);

        switch (type)
        {
        case giblorb_ID_MOD:
            source = new OpenMPTSource(data, repeats);
            break;
        case giblorb_ID_AIFF:
        case giblorb_ID_FORM:
        case giblorb_ID_OGG:
        case giblorb_ID_WAVE:
            source = new SndfileSource(data, repeats);
            break;
        default:
            return 0;
        }
    }
    catch (const std::runtime_error &)
    {
        return 0;
    }

    source->open(QIODevice::ReadOnly);

    QAudioFormat format;
    format.setSampleRate(source->rate());
    format.setChannelCount(source->channels());
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

    QAudioDeviceInfo info(QAudioDeviceInfo::defaultOutputDevice());
    if (!info.isFormatSupported(format))
        return 0;

    chan->audio = std::make_unique<QAudioOutput>(format, nullptr);

    auto on_change = [&](QAudio::State state) {
        switch (state)
        {
        case QAudio::State::IdleState:
            gli_event_store(evtype_SoundNotify, nullptr, 0, 1);
            gli_notification_waiting();
            break;
        default:
            break;
        }
    };

    QObject::connect(chan->audio.get(), &QAudioOutput::stateChanged, on_change);

    chan->audio->setVolume((double)chan->current_volume / GLK_MAXVOLUME);

    if (!chan->paused)
        chan->audio->start(source);

    source->set_audio_buffer_size(chan->audio->bufferSize());

    return 1;
}

void glk_schannel_pause(schanid_t chan)
{
    if (chan == nullptr)
    {
        gli_strict_warning("schannel_pause: invalid id.");
        return;
    }

    chan->paused = true;

    if (chan->audio)
        chan->audio->suspend();
}

void glk_schannel_unpause(schanid_t chan)
{
    if (chan == nullptr)
    {
        gli_strict_warning("schannel_unpause: invalid id.");
        return;
    }

    chan->paused = false;

    if (chan->audio)
        chan->audio->resume();
}

void glk_schannel_stop(schanid_t chan)
{
    if (chan == nullptr)
    {
        gli_strict_warning("schannel_stop: invalid id.");
        return;
    }

    if (!chan->audio)
        return;

    chan->audio->stop();
    chan->audio.reset(nullptr);
}
