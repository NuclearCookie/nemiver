// Author: Fabien Parent
/*
 *This file is part of the Nemiver project
 *
 *Nemiver is free software; you can redistribute
 *it and/or modify it under the terms of
 *the GNU General Public License as published by the
 *Free Software Foundation; either version 2,
 *or (at your option) any later version.
 *
 *Nemiver is distributed in the hope that it will
 *be useful, but WITHOUT ANY WARRANTY;
 *without even the implied warranty of
 *MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *See the GNU General Public License for more details.
 *
 *You should have received a copy of the
 *GNU General Public License along with Nemiver;
 *see the file COPYING.
 *If not, write to the Free Software Foundation,
 *Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *See COPYRIGHT file copyright information.
 */

#include "nmv-perf-engine.h"
#include "nmv-call-graph-node.h"
#include "common/nmv-proc-utils.h"
#include "common/nmv-str-utils.h"
#include <istream>
#include <stack>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <cstdio>

NEMIVER_BEGIN_NAMESPACE (nemiver)

const char *const PERF_ANNOTATE_PARSING_DOMAIN = "perf-annotate-parsing-domain";
const char *const PERF_REPORT_PARSING_DOMAIN = "perf-report-parsing-domain";

using common::DynModIfaceSafePtr;
using common::FreeUnref;
using common::DefaultRef;

struct PerfEngine::Priv {
    int perf_pid;
    int master_pty_fd;
    int perf_stdout_fd;
    int perf_stderr_fd;
    Glib::RefPtr<Glib::IOChannel> perf_stdout_channel;
    UString record_filepath;

    std::stack<CallGraphNodeSafePtr> call_stack;
    CallGraphSafePtr call_graph;
    UString annotation_buffer;
    UString annotated_symbol;

    sigc::signal<void, CallGraphSafePtr> report_done_signal;
    sigc::signal<void, const UString&> record_done_signal;
    sigc::signal<void, const UString&, const UString&> symbol_annotated_signal;

    Priv () :
        perf_pid (0),
        master_pty_fd (0),
        perf_stdout_fd (0),
        perf_stderr_fd (0),
        perf_stdout_channel (0),
        call_graph (0)
    {
    }

    bool
    on_wait_for_record_to_exit ()
    {
        int status = 0;
        pid_t pid = waitpid (perf_pid, &status, WNOHANG);
        bool is_terminated = WIFEXITED (status) || WIFSIGNALED (status);

        if (pid == perf_pid && is_terminated) {
            g_spawn_close_pid (perf_pid);
            perf_pid = 0;
            master_pty_fd = 0;
            perf_stdout_fd = 0;
            perf_stderr_fd = 0;

            record_done_signal.emit (record_filepath);

            return false;
        }

        return true;
    }

    bool
    on_wait_for_symbol_annotation_to_exit ()
    {
        int status = 0;
        pid_t pid = waitpid (perf_pid, &status, WNOHANG);
        bool is_terminated = WIFEXITED (status) || WIFSIGNALED (status);

        if (pid == perf_pid && is_terminated) {
            NEMIVER_TRY;
            THROW_IF_FAIL (perf_stdout_channel);
            perf_stdout_channel->close ();
            perf_stdout_channel.reset ();

            g_spawn_close_pid (perf_pid);
            perf_pid = 0;
            master_pty_fd = 0;
            perf_stdout_fd = 0;
            perf_stderr_fd = 0;

            THROW_IF_FAIL (WIFEXITED (status));
            if (WEXITSTATUS (status) == 0) {
                symbol_annotated_signal.emit
                    (annotated_symbol, annotation_buffer);
            }

            NEMIVER_CATCH_NOX

            return false;
        }

        return true;
    }

    bool
    on_wait_for_report_to_exit ()
    {
        int status = 0;
        pid_t pid = waitpid (perf_pid, &status, WNOHANG);
        bool is_terminated = WIFEXITED (status) || WIFSIGNALED (status);

        if (pid == perf_pid && is_terminated) {
            NEMIVER_TRY;

            THROW_IF_FAIL (perf_stdout_channel);
            perf_stdout_channel->close ();
            perf_stdout_channel.reset ();

            g_spawn_close_pid (perf_pid);
            perf_pid = 0;
            master_pty_fd = 0;
            perf_stdout_fd = 0;
            perf_stderr_fd = 0;

            THROW_IF_FAIL (WIFEXITED (status) && WEXITSTATUS (status) == 0);
            report_done_signal.emit (call_graph);

            NEMIVER_CATCH_NOX;
            return false;
        }

        return true;
    }

    void
    parse_top_level_symbol (std::vector<UString> &a_tokens)
    {
        THROW_IF_FAIL (a_tokens.size ());
        THROW_IF_FAIL (a_tokens[0].size () >= 5);
    
        float overhead = 0.0;
        std::istringstream is (a_tokens[0].substr(0, a_tokens[0].size () - 1));
        is >> overhead;

        CallGraphNodeSafePtr node (new CallGraphNode);
        THROW_IF_FAIL (node);
        node->overhead (overhead);
        node->command (a_tokens[1]);
        node->shared_object (a_tokens[2]);

        std::vector<UString>::const_iterator first_symbol_token = a_tokens.begin () + 4;
        std::vector<UString>::const_iterator last_symbol_token = a_tokens.end ();
        node->symbol (str_utils::join (first_symbol_token, last_symbol_token));
        call_graph->add_child (node);

        while (call_stack.size ()) {
            call_stack.pop ();
        }
        call_stack.push (node);
    }

    void
    parse_child_symbol (std::vector<UString> &a_tokens)
    {
        THROW_IF_FAIL (a_tokens.size ());
        if (a_tokens[a_tokens.size () - 1] == "|") {
            return;
        } else if (a_tokens[0] == "---") {
            return;
        }

        size_t i = 0;
        for (; i < a_tokens.size ()
               && a_tokens[i].size ()
               && (a_tokens[i][0] == '|' || a_tokens[i][0] == '-')
             ; i++) {
        }

        while (call_stack.size () > i) {
            call_stack.pop ();
        }

        if (!call_stack.size ()) {
            return;
        }

        THROW_IF_FAIL (call_stack.size ());
        THROW_IF_FAIL (a_tokens.size () >= 2);
        if (a_tokens[a_tokens.size () - 2].find ("--") == std::string::npos) {
            return;
        }

        float overhead = 0.0;
        std::istringstream is (a_tokens[a_tokens.size () - 2].substr (3));
        is >> overhead;

        CallGraphNodeSafePtr node (new CallGraphNode);
        THROW_IF_FAIL (node);
        node->overhead (overhead);

        if (a_tokens.size () > 4)
        {
            std::vector<UString>::const_iterator first_symbol_token =
                a_tokens.begin () + 4;
            std::vector<UString>::const_iterator last_symbol_token =
                a_tokens.end ();
            node->symbol
                (str_utils::join (first_symbol_token, last_symbol_token));
        }
        else
        {
            node->symbol (a_tokens[a_tokens.size () - 1]);
        }

        THROW_IF_FAIL (call_stack.top ());
        call_stack.top ()->add_child (node);

        THROW_IF_FAIL (i - 1 < a_tokens.size ());
        if (a_tokens[i - 1][0] == '-') {
            call_stack.pop ();
        }
        call_stack.push (node);
    }

    void
    build_call_graph (UString &a_line)
    {
        if (!a_line.size () || a_line.empty () || a_line[0] == '#') {
            return;
        }

        a_line = a_line.substr(0, a_line.size () - 1);
        std::vector<UString> split = str_utils::split (a_line, " ");
        if (!split.size ()) {
            return;
        }

        UString token = split[0];
        if (token.size () && token[token.size () - 1] == '%') {
            parse_top_level_symbol (split);
        } else {
            parse_child_symbol (split);
        }
    }

    bool
    read_symbol_annotation (Glib::IOCondition)
    {
        NEMIVER_TRY

        THROW_IF_FAIL (perf_stdout_channel);

        UString line;
        Glib::IOStatus status;

        do {
            status = perf_stdout_channel->read_line (line);
            LOG_D (line, PERF_ANNOTATE_PARSING_DOMAIN);
            annotation_buffer += line;
        } while (status == Glib::IO_STATUS_NORMAL);

        NEMIVER_CATCH_NOX

        return false;
    }

    bool
    read_report (Glib::IOCondition)
    {
        NEMIVER_TRY

        THROW_IF_FAIL (perf_stdout_channel);

        UString line;
        Glib::IOStatus status;

        do {
            status = perf_stdout_channel->read_line (line);
            LOG_D (line, PERF_REPORT_PARSING_DOMAIN);
            build_call_graph (line);
        } while (status == Glib::IO_STATUS_NORMAL);

        NEMIVER_CATCH_NOX

        return false;
    }

    ~Priv ()
    {
        LOG_D ("delete", "destructor-domain");
    }
}; // end sturct PerfEngine::Priv

PerfEngine::PerfEngine (DynamicModule *a_dynmod) :
    IProfiler (a_dynmod),
    m_priv (new Priv)
{
}

PerfEngine::~PerfEngine ()
{
    LOG_D ("delete", "destructor-domain");
}

void
PerfEngine::attach_to_pid (int a_pid,
                           bool a_scale_counter_values,
                           bool a_do_callgraph,
                           bool a_child_inherit_counters)
{
    std::vector<UString> argv;
    argv.push_back ("--pid");
    argv.push_back (UString::compose ("%1", a_pid));

    record (argv, a_scale_counter_values, a_do_callgraph,
            a_child_inherit_counters);
}

void
PerfEngine::record (const std::vector<UString> &a_argv,
                    bool a_scale_counter_values,
                    bool a_do_callgraph,
                    bool a_child_inherit_counters)
{
    SafePtr<char, DefaultRef, FreeUnref> tmp_filepath (tempnam(0, 0));
    THROW_IF_FAIL (tmp_filepath);

    THROW_IF_FAIL (m_priv);
    m_priv->record_filepath = tmp_filepath.get ();

    std::vector<UString> argv;
    argv.push_back ("perf");
    argv.push_back ("record");

    if (a_scale_counter_values) {
        argv.push_back ("-l");
    }

    if (a_do_callgraph) {
        argv.push_back ("--call-graph");
    }

    if (a_child_inherit_counters) {
        argv.push_back ("--inherit");
    }

    argv.push_back ("--output");
    argv.push_back (m_priv->record_filepath);
    argv.insert (argv.end (), a_argv.begin (), a_argv.end ());

    bool is_launched = common::launch_program (argv,
                                               m_priv->perf_pid,
                                               m_priv->master_pty_fd,
                                               m_priv->perf_stdout_fd,
                                               m_priv->perf_stderr_fd);
    THROW_IF_FAIL (is_launched);

    Glib::RefPtr<Glib::MainContext> context = Glib::MainContext::get_default ();
    context->signal_idle ().connect (sigc::mem_fun
        (m_priv.get (), &PerfEngine::Priv::on_wait_for_record_to_exit));
}

void
PerfEngine::record (const UString &a_program_path,
                    const std::vector<UString> &a_argv,
                    bool a_scale_counter_values,
                    bool a_do_callgraph,
                    bool a_child_inherit_counters)
{
    std::vector<UString> argv;
    argv.push_back ("--");
    argv.push_back (a_program_path);
    argv.insert (argv.end (), a_argv.begin (), a_argv.end ());

    record (argv, a_scale_counter_values, a_do_callgraph,
            a_child_inherit_counters);
}

void
PerfEngine::stop_recording ()
{
    THROW_IF_FAIL (m_priv);
    THROW_IF_FAIL (m_priv->perf_pid);
    kill(m_priv->perf_pid, SIGINT);
}

void
PerfEngine::report (const UString &a_data_file)
{
    std::vector<UString> argv;
    argv.push_back ("perf");
    argv.push_back ("report");
    argv.push_back ("--stdio");
    argv.push_back ("-i");
    argv.push_back (a_data_file);

    THROW_IF_FAIL (m_priv);
    m_priv->record_filepath = a_data_file;

    bool is_launched = common::launch_program (argv,
                                               m_priv->perf_pid,
                                               m_priv->master_pty_fd,
                                               m_priv->perf_stdout_fd,
                                               m_priv->perf_stderr_fd);
    THROW_IF_FAIL (is_launched);

    m_priv->call_graph.reset (new CallGraph);

    m_priv->perf_stdout_channel =
        Glib::IOChannel::create_from_fd (m_priv->perf_stdout_fd);

    Glib::RefPtr<Glib::IOSource> io_source =
        m_priv->perf_stdout_channel->create_watch (Glib::IO_IN);
    io_source->connect (sigc::mem_fun
        (m_priv.get (), &PerfEngine::Priv::read_report));
    io_source->attach ();

    Glib::RefPtr<Glib::MainContext> context = Glib::MainContext::get_default ();
    context->signal_idle ().connect (sigc::mem_fun
        (m_priv.get (), &PerfEngine::Priv::on_wait_for_report_to_exit));
}

void
PerfEngine::annotate_symbol (const UString &a_symbol_name)
{
    THROW_IF_FAIL (m_priv);
    m_priv->annotation_buffer.clear ();
    m_priv->annotated_symbol = a_symbol_name;

    std::vector<UString> argv;
    argv.push_back ("perf");
    argv.push_back ("annotate");
    argv.push_back ("--stdio");
    argv.push_back ("-i");
    argv.push_back (m_priv->record_filepath);
    argv.push_back (a_symbol_name);

    bool is_launched = common::launch_program (argv,
                                               m_priv->perf_pid,
                                               m_priv->master_pty_fd,
                                               m_priv->perf_stdout_fd,
                                               m_priv->perf_stderr_fd);
    THROW_IF_FAIL (is_launched);

    m_priv->perf_stdout_channel =
        Glib::IOChannel::create_from_fd (m_priv->perf_stdout_fd);

    Glib::RefPtr<Glib::IOSource> io_source =
        m_priv->perf_stdout_channel->create_watch (Glib::IO_IN);
    io_source->connect (sigc::mem_fun
        (m_priv.get (), &PerfEngine::Priv::read_symbol_annotation));
    io_source->attach ();

    Glib::RefPtr<Glib::MainContext> context = Glib::MainContext::get_default ();
    context->signal_idle ().connect (sigc::mem_fun
        (m_priv.get (),
         &PerfEngine::Priv::on_wait_for_symbol_annotation_to_exit));
}

sigc::signal<void, CallGraphSafePtr>
PerfEngine::report_done_signal () const
{
    THROW_IF_FAIL (m_priv);
    return m_priv->report_done_signal;
}

sigc::signal<void, const UString&>
PerfEngine::record_done_signal () const
{
    THROW_IF_FAIL (m_priv);
    return m_priv->record_done_signal;
}

sigc::signal<void, const UString&, const UString&>
PerfEngine::symbol_annotated_signal () const
{
    THROW_IF_FAIL (m_priv);
    return m_priv->symbol_annotated_signal;
}

//****************************
//</GDBEngine methods>
//****************************

class ProfilerEngineModule : public DynamicModule {

public:

    void get_info (Info &a_info) const
    {
        const static Info s_info ("profilerengine",
                                  "The perf profiler engine backend. "
                                  "Implements the IProfiler interface",
                                  "1.0");
        a_info = s_info;
    }

    void do_init ()
    {
    }

    bool lookup_interface (const std::string &a_iface_name,
                           DynModIfaceSafePtr &a_iface)
    {
        if (a_iface_name == "IProfiler") {
            a_iface.reset (new PerfEngine (this));
        } else {
            return false;
        }
        return true;
    }
};//end class ProfilerEngineModule

NEMIVER_END_NAMESPACE (nemiver)

//the dynmod initial factory.
extern "C" {
bool
NEMIVER_API nemiver_common_create_dynamic_module_instance (void **a_new_instance)
{
    *a_new_instance = new nemiver::ProfilerEngineModule ();
    return (*a_new_instance != 0);
}

}//end extern C