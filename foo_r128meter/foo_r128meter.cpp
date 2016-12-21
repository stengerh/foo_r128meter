#include "stdafx.h"

DECLARE_COMPONENT_VERSION("R128 Meter", "0.1.0", "Measures and displays loudness according to EBU-R 128")

class r128meter {
private:
    unsigned m_samplerate;
    unsigned m_channel_count;
    unsigned m_channel_config;
    double m_segment_duration;
    ebur128_state * m_state;

public:
    r128meter() : m_samplerate(0), m_channel_count(0), m_channel_config(0), m_segment_duration(0.0), m_state(nullptr) {
    }

    ~r128meter() {
        if (m_state) {
            ebur128_destroy(&m_state);
        }
    }

    void add_chunk(audio_chunk *p_chunk) {
        update_parameters(p_chunk);

        int rval = ebur128_add_frames_float(m_state, p_chunk->get_data(), p_chunk->get_sample_count());
        if (rval == EBUR128_SUCCESS) {
            double duration = p_chunk->get_duration();
            m_segment_duration += duration;
        }
    }

    bool get_momentary_loudness(double *p_loudness, double *p_stable_in = nullptr) {
        if (!m_state) return false;
        int rval = ebur128_loudness_momentary(m_state, p_loudness);
        if (p_stable_in) *p_stable_in = pfc::max_t(0.0, 0.4 - m_segment_duration);
        return (rval == EBUR128_SUCCESS);
    }

    bool get_shortterm_loudness(double *p_loudness, double *p_stable_in = nullptr) {
        if (!m_state) return false;
        int rval = ebur128_loudness_shortterm(m_state, p_loudness);
        if (p_stable_in) *p_stable_in = pfc::max_t(0.0, 3.0 - m_segment_duration);
        if (rval != EBUR128_SUCCESS) {
            switch (rval) {
            case EBUR128_SUCCESS:
                console::formatter() << "EBUR128_SUCCESS";
                break;
            case EBUR128_ERROR_NOMEM:
                console::formatter() << "EBUR128_ERROR_NOMEM";
                break;
            case EBUR128_ERROR_INVALID_MODE:
                console::formatter() << "EBUR128_ERROR_INVALID_MODE";
                break;
            case EBUR128_ERROR_INVALID_CHANNEL_INDEX:
                console::formatter() << "EBUR128_ERROR_INVALID_CHANNEL_INDEX";
                break;
            case EBUR128_ERROR_NO_CHANGE:
                console::formatter() << "EBUR128_ERROR_NO_CHANGE";
                break;
            default:
                console::formatter() << rval;
                break;
            }
        }
        return (rval == EBUR128_SUCCESS);
    }

    bool get_loudness_range(double *p_loudness_range) {
        if (!m_state) return false;
        int rval = ebur128_loudness_range(m_state, p_loudness_range);
        return (rval == EBUR128_SUCCESS);
    }

private:
    bool update_parameters(audio_chunk *p_chunk) {
        unsigned int sample_rate = p_chunk->get_sample_rate();
        unsigned int channel_count = p_chunk->get_channel_count();
        unsigned int channel_config = p_chunk->get_channel_config();

        if ( !m_state ) {
            m_state = ebur128_init(channel_count, sample_rate, EBUR128_MODE_M | EBUR128_MODE_S);
            if ( !m_state ) return false;
        }

        return change_parameters(sample_rate, channel_count, channel_config);
    }

    bool change_parameters(unsigned int p_sample_rate, unsigned int p_channel_count, unsigned int p_channel_config) {

	    int rval = ebur128_change_parameters( m_state, p_channel_count, p_sample_rate );

        if ( rval != EBUR128_SUCCESS && rval != EBUR128_ERROR_NO_CHANGE ) return false;

	    for ( unsigned int channel_index = 0; channel_index < p_channel_count; channel_index++ )
	    {
		    int channel = EBUR128_UNUSED;

		    switch ( audio_chunk::g_extract_channel_flag( p_channel_config, channel_index ) )
		    {
		    case audio_chunk::channel_front_left:   channel = EBUR128_LEFT;           break;
		    case audio_chunk::channel_front_right:  channel = EBUR128_RIGHT;          break;
		    case audio_chunk::channel_front_center: channel = EBUR128_CENTER;         break;
		    case audio_chunk::channel_back_left:    channel = EBUR128_LEFT_SURROUND;  break;
		    case audio_chunk::channel_back_right:   channel = EBUR128_RIGHT_SURROUND; break;
		    }

            rval = ebur128_set_channel( m_state, channel_index, channel );

            if ( rval != 0 ) return false;
	    }

	    return true;
    }
};

class r128meter_ui_element : public ui_element_instance, public CWindowImpl<r128meter_ui_element> {
protected:
    ui_element_instance_callback::ptr m_callback;
    visualisation_stream_v2::ptr m_stream;
    double m_last_time;
    r128meter m_meter;

    CStatic m_label;
    CBrush m_brushBackground;

public:
    BEGIN_MSG_MAP_EX(r128meter_ui_element)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_TIMER(OnTimer)
        MSG_WM_SIZE(OnSize)
        MSG_WM_CTLCOLORSTATIC(OnCtlColorStatic)
    END_MSG_MAP()

    static GUID g_get_guid() {
        // {364EB73D-7E70-4DF3-B8D3-31F46B882FF5}
        static const GUID r128meter_ui_element_guid = 
        { 0x364eb73d, 0x7e70, 0x4df3, { 0xb8, 0xd3, 0x31, 0xf4, 0x6b, 0x88, 0x2f, 0xf5 } };

        return r128meter_ui_element_guid;
    }

    static GUID g_get_subclass() {
        return ui_element_subclass_playback_visualisation;
    }

    static void g_get_name(pfc::string_base & p_name) {
        p_name.set_string("R128 Meter");
    }

    static const char * g_get_description() {
        return "Measures loudness according to EBU-R 128";
    }

    static ui_element_config::ptr g_get_default_configuration() {
        ui_element_config_builder builder;
        return builder.finish(g_get_guid());
    }

    r128meter_ui_element(ui_element_config::ptr p_config, ui_element_instance_callback::ptr p_callback) : m_callback(p_callback), m_last_time(0.0) {
        set_configuration(p_config);
    }

    void initialize_window(HWND p_parent) {
        Create(p_parent);
    }

    virtual HWND get_wnd() {
        return NULL;
    }

    virtual void set_configuration(ui_element_config::ptr p_config) {

    }

    virtual ui_element_config::ptr get_configuration() {
        return g_get_default_configuration();
    }

    virtual GUID get_guid() {
        return g_get_guid();
    }

    virtual GUID get_subclass() {
        return g_get_subclass();
    }

    virtual void notify(const GUID & p_what, t_size p_param1, const void * p_param2, t_size p_param2size) {
        if (p_what == ui_element_notify_colors_changed) {
            m_brushBackground = nullptr;
            m_label.RedrawWindow();
        } else if (p_what == ui_element_notify_font_changed) {
            m_label.SetFont(m_callback->query_font_ex(ui_font_default));
        }
    }

    enum {
        ID_TIMER_UPDATE = 1
    };

    int OnCreate(LPCREATESTRUCT lpCreateStruct) {
        visualisation_manager::ptr manager = standard_api_create_t<visualisation_manager>();
        manager->create_stream(m_stream, 0);
        m_stream->request_backlog(1.0);
        m_stream->set_channel_mode(visualisation_stream_v2::channel_mode_default);
        SetTimer(ID_TIMER_UPDATE, 100);
        m_label.Create(*this, 0, TEXT("R128 Meter"), WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_NOPREFIX);
        notify(ui_element_notify_colors_changed, 0, nullptr, 0);
        notify(ui_element_notify_font_changed, 0, nullptr, 0);
        return 0;
    }

    void OnDestroy() {
        KillTimer(ID_TIMER_UPDATE);
        m_stream.release();
    }

    void OnTimer(UINT_PTR nIDEvent) {
        switch (nIDEvent) {
        case ID_TIMER_UPDATE:
            {
                double time;
                if (m_stream->get_absolute_time(time)) {
                    if (time < m_last_time) {
                        m_last_time = 0.0;
                    }
                    if (time > m_last_time) {
                        audio_chunk_impl chunk;
                        if (m_stream->get_chunk_absolute(chunk, m_last_time, time - m_last_time)) {
                            m_meter.add_chunk(&chunk);
                            pfc::string_formatter formatter;
                            double loudness;
                            double stable_in;
                            if (m_meter.get_momentary_loudness(&loudness, &stable_in)) {
                                formatter << "momentary loudness: " << pfc::format_float(loudness, 0, 1) << " LUFS";
                                if (stable_in > 0.0) {
                                    formatter << " (stable in " << pfc::format_float(ceil(stable_in), 0, 0) << " s)";
                                }
                                formatter << "\r\n";
                            }
                            if (m_meter.get_shortterm_loudness(&loudness, &stable_in)) {
                                formatter << "short-term loudness: " << pfc::format_float(loudness, 0, 1) << " LUFS";
                                if (stable_in > 0.0) {
                                    formatter << " (stable in " << pfc::format_float(ceil(stable_in), 0, 0) << " s)";
                                }
                                formatter << "\r\n";
                            }
                            m_label.SetWindowText(pfc::stringcvt::string_os_from_utf8(formatter));
                        } else {
                            console::formatter() << "R128 Meter: no chunk available, time = " << time << ", last time = " << m_last_time;
                        }
                    }
                    m_last_time = time;
                }
            }
            break;
        default:
            SetMsgHandled(FALSE);
            break;
        }
    }

    void OnSize(UINT nType, CSize size) {
        m_label.SetWindowPos(nullptr, 0, 0, size.cx, size.cy, SWP_NOZORDER);
    }

    HBRUSH OnCtlColorStatic(CDCHandle dc, CStatic wndStatic) {
        t_ui_color color_text = 0x000000;
        t_ui_color color_background = 0xffffff;

        m_callback->query_color(ui_color_text, color_text);
        m_callback->query_color(ui_color_background, color_background);

        dc.SetTextColor(color_text);
        dc.SetBkColor(color_background);
        if (m_brushBackground.IsNull()) {
            m_brushBackground.CreateSolidBrush(color_background);
        }
        return m_brushBackground;
    }
};

static service_factory_single_t< ui_element_impl_withpopup< r128meter_ui_element > > g_r128meter_ui_element_factory;

// {FE4E2726-4E2C-4DD4-B6AA-02161280953A}
static const GUID r128meter_mainmenu_command_guid = 
{ 0xfe4e2726, 0x4e2c, 0x4dd4, { 0xb6, 0xaa, 0x2, 0x16, 0x12, 0x80, 0x95, 0x3a } };

bool change_parameters( ebur128_state * state, unsigned sample_rate, unsigned channels, unsigned channel_config )
{
	int rval = ebur128_change_parameters( state, channels, sample_rate );

    if ( rval != EBUR128_SUCCESS && rval != EBUR128_ERROR_NO_CHANGE ) return false;

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

        rval = ebur128_set_channel( state, i, channel );

        if ( rval != 0 ) return false;
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
                        m_state = ebur128_init(channel_count, sample_rate, EBUR128_MODE_M | EBUR128_MODE_S);

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
                    double loudness;
                    if (!ebur128_loudness_momentary(m_state, &loudness)) {
                        console::formatter() << "momentary loudness: " << pfc::format_float(loudness, 0, 1) << " LUFS";
                    }
                    if (!ebur128_loudness_shortterm(m_state, &loudness)) {
                        console::formatter() << "short-term loudness: " << pfc::format_float(loudness, 0, 1) << " LUFS";
                    }
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

//static mainmenu_commands_factory_t<r128meter_mainmenu_commands> g_r128meter_mainmenu_commands_factory;
