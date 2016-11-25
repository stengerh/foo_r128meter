#include "stdafx.h"

DECLARE_COMPONENT_VERSION("R128 Meter", "1.0.0", "Measures and displays loudness according to EBU-R 128")

// {FE4E2726-4E2C-4DD4-B6AA-02161280953A}
static const GUID r128meter_mainmenu_command_guid = 
{ 0xfe4e2726, 0x4e2c, 0x4dd4, { 0xb6, 0xaa, 0x2, 0x16, 0x12, 0x80, 0x95, 0x3a } };

bool change_parameters( ebur128_state * state, unsigned sample_rate, unsigned channels, unsigned channel_config )
{
	int rval = ebur128_change_parameters( state, channels, sample_rate );

	if ( rval != 0 && rval != 2 ) return false;

	for ( unsigned i = 0; i < channels; i++ )
	{
		int channel = EBUR128_UNUSED;
		switch ( audio_chunk::g_extract_channel_flag( channel_config, i ) )
		{
		case audio_chunk::channel_front_left:   channel = EBUR128_LEFT;           break;
		case audio_chunk::channel_front_right:  channel = EBUR128_RIGHT;          break;
		case audio_chunk::channel_front_center: channel = EBUR128_CENTER;         break;
		case audio_chunk::channel_back_left:    channel = EBUR128_LEFT_SURROUND;  break;
		case audio_chunk::channel_back_right:   channel = EBUR128_RIGHT_SURROUND; break;
		}
		ebur128_set_channel( state, i, channel );
	}

	return true;
}

class r128meter_thread : public pfc::thread {
private:
    visualisation_stream_v2::ptr m_stream;
    ebur128_state * m_state;

public:
    r128meter_thread(const visualisation_stream_v2::ptr & p_stream) : m_stream(p_stream), m_state(nullptr) {
    }

    virtual ~r128meter_thread() {
        waitTillDone();
        if (m_state) {
            ebur128_destroy(&m_state);
        }
    }

    virtual void threadProc() {
        unsigned sample_rate = 0;
        unsigned channel_count = 0;
        unsigned channel_config = 0;
        double last_time = 0.0;
        while (true) {
            Sleep(100);
            double time;
            if (m_stream->get_absolute_time(time)) {
                console::formatter() << "time: " << time;
                if (time > last_time) {
                audio_chunk_impl chunk;
                if (m_stream->get_chunk_absolute(chunk, last_time, time - last_time)) {
                    console::formatter() << "got chunk, length: " << chunk.get_duration();
                    if (!m_state) {
                        sample_rate = chunk.get_sample_rate();
                        channel_count = chunk.get_channel_count();
                        channel_config = chunk.get_channel_config();
                        m_state = ebur128_init(channel_count, sample_rate, EBUR128_MODE_I);

                        if ( !m_state ) {
                            throw std::bad_alloc();
                        }

                        if ( !change_parameters( m_state, sample_rate, channel_count, channel_config ) ) {
				            throw std::bad_alloc();
                        }
                    }
                    if ((sample_rate != chunk.get_sample_rate()) || (channel_count != chunk.get_channel_count()) || (channel_config != chunk.get_channel_config())) {
                        sample_rate = chunk.get_sample_rate();
                        channel_count = chunk.get_channel_count();
                        channel_config = chunk.get_channel_config();
                        if ( !change_parameters( m_state, sample_rate, channel_count, channel_config ) ) {
				            throw std::bad_alloc();
                        }
                    }
                    if ( ebur128_add_frames_float( m_state, chunk.get_data(), chunk.get_sample_count() ) ) break;
                    double loudness_momentary = ebur128_loudness_momentary(m_state);
                    //ebur128_gated_loudness_cleanup(m_state, 1);
                    console::formatter() << "momentary loudness: " << loudness_momentary << " LKFS";
                } else {
                    console::formatter() << "got no chunk";
                }
                }
                last_time = time;
            } else {
                console::formatter() << "time: N/A";
            }
        }
    }
};

class r128meter_mainmenu_commands : public mainmenu_commands {
public:
	virtual t_uint32 get_command_count() {
        return 1;
    }

	virtual GUID get_command(t_uint32 p_index) {
        switch (p_index) {
        case 0:
            return r128meter_mainmenu_command_guid;
        default:
            throw pfc::exception_invalid_params("Index out of range");
        }
    }

	virtual void get_name(t_uint32 p_index,pfc::string_base & p_out) {
        switch (p_index) {
        case 0:
            p_out = "R128 Meter";
            break;
        default:
            throw pfc::exception_invalid_params("Index out of range");
        }
    }


	virtual bool get_description(t_uint32 p_index,pfc::string_base & p_out) {
        switch (p_index) {
        case 0:
            p_out = "Toggles R128 meter.";
            return true;
        default:
            throw pfc::exception_invalid_params("Index out of range");
        }
        return false;
    }

	virtual GUID get_parent() {
        return mainmenu_groups::view_visualisations;
    }

	virtual void execute(t_uint32 p_index,service_ptr_t<service_base> p_callback) {
        switch (p_index) {
        case 0:
            {
                visualisation_stream_v2::ptr vs;
                visualisation_manager::ptr vm = standard_api_create_t<visualisation_manager>();
                vm->create_stream(vs, 0);
                vs->request_backlog(1.0);
                vs->set_channel_mode(visualisation_stream_v2::channel_mode_default);

                r128meter_thread* thread = new r128meter_thread(vs);
                thread->start();
            }
            break;
        default:
            throw pfc::exception_invalid_params("Index out of range");
        }
    }
};

static mainmenu_commands_factory_t<r128meter_mainmenu_commands> g_r128meter_mainmenu_commands_factory;
