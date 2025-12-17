#include "ftrace.h"

#ifdef HAVE_FTRACE

#include <algorithm>
#include <climits>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include "mesa/util/macros.h"
#include "string_utils.h"

namespace FTrace {

FTrace::FTrace(const overlay_params::ftrace_options& options) {
   assert(options.enabled);

   trace_pipe_fd = open("/sys/kernel/tracing/trace_pipe", O_RDONLY | O_NONBLOCK);
   if (trace_pipe_fd == -1) {
       SPDLOG_ERROR("could not open ftrace pipe: {}", strerror(errno));
       return;
   }

   thread = std::thread(&FTrace::ftrace_thread, this);
   pthread_setname_np(thread.native_handle(), "mangohud-ftrace");

   for (auto& tp : options.tracepoints) {
      data.tracepoints.push_back(tp);
      collection.map.emplace(tp, Tracepoint::CollectionValue { });

      switch (tp->type) {
      case TracepointType::Histogram:
         SPDLOG_DEBUG("ftrace: will collect tracepoint '{}' for histogram display",
                      tp->name);
         break;
      case TracepointType::LineGraph:
         SPDLOG_DEBUG("ftrace: will collect tracepoint '{}' (parameter '{}') for line graph display",
                      tp->name, tp->field_name);
         break;
      case TracepointType::Label:
         SPDLOG_DEBUG("ftrace: will collect tracepoint '{}' (parameter '{}') for label display",
                      tp->name, tp->field_name);
         break;
      default:
         SPDLOG_ERROR("ftrace: unknown tracepoint type for tracepoint '{}'",
                      tp->name);
      }
   }
}

FTrace::~FTrace()
{
    stop_thread = true;
    if (thread.joinable())
        thread.join();

    if (trace_pipe_fd != -1)
        close(trace_pipe_fd);
}

void FTrace::ftrace_thread()
{
    struct {
        std::array<char, 4096> data;
        size_t size { 0 };
    } buffer;

    while (!stop_thread) {
        struct pollfd fd = {
            .fd = trace_pipe_fd,
            .events = POLLIN,
            .revents = 0,
        };
        int ret = poll(&fd, 1, 500);
        if (ret < 0) {
            SPDLOG_ERROR("FTrace: polling on trace_pipe failed: {}", strerror(errno));
            break;
        }

        if (!(fd.revents & POLLIN))
            continue;

        ssize_t size_read = read(trace_pipe_fd, &buffer.data[buffer.size], buffer.data.size() - buffer.size);
        if (size_read < 0) {
            SPDLOG_ERROR("FTrace: reading from trace_pipe failed: {}", strerror(errno));
            break;
        }
        buffer.size += size_t(size_read);

        {
            char *it = buffer.data.begin();
            char *end_it = std::next(it, buffer.size);

            while (std::distance(it, end_it)) {
                char *newline_it = std::find(it, end_it, '\n');
                if (newline_it == end_it)
                    break;

                handle_ftrace_entry(std::string { it, size_t(std::distance(it, newline_it)) });
                it = std::next(newline_it, 1);
            }

            buffer.size = std::distance(it, end_it);
            if (buffer.size)
                std::move(it, end_it, buffer.data.begin());
        }
    }
}

static std::string get_field_value(std::string fields_str, std::string target_field_name)
{
   // Parsing print-formatted ftrace entries isn't ideal because there's no standardized
   // way of reporting trace event fields. ftrace will also print out numerical values
   // in both decimal and hexadecimal. It's still the simplest approach as otherwise we'd
   // have to wrangle with raw trace data that we don't have access to, and trace event
   // field formats that are hard to define and apply.

   // Most common print format is `field=value`, with `value` possibly containing spaces.
   // Iteration below accounts for that and returns the field value for the desired name,
   // if present.

   auto fields_data = str_tokenize(fields_str, " ");
   for (auto it = fields_data.begin(); it != fields_data.end();) {
      auto assign_pos = it->find('=');
      if (assign_pos == std::string::npos) {
         ++it;
         continue;
      }

      auto field_name = it->substr(0, assign_pos);
      auto field_value = it->substr(std::min(it->size(), assign_pos + 1));

      ++it;
      while (it != fields_data.end()) {
         if (it->find('=') != std::string::npos)
            break;

         field_value += " " + *it;
         ++it;
      }

      if (field_name == target_field_name)
         return field_value;
   }

   return { };
}

void FTrace::handle_ftrace_entry(std::string entry)
{
   std::unique_lock<std::mutex> lock(collection.mutex);

   for (auto& collection_entry : collection.map) {
      auto& tp = collection_entry.first;

      // Search for the tracepoint name in the entry. It's expected either
      // at the beginning of string or with a space before it.
      auto name_pos = entry.find(tp->name + ":");
      if (name_pos == std::string::npos)
         continue;
      if (!(name_pos == 0 || (name_pos > 0 && entry[name_pos - 1] == ' ')))
         continue;

      // Remainder of entry is field data. We use it as necessary,
      // depending on the tracepoint type.
      auto fields_str = entry.substr(name_pos + tp->name.size() + 1);

      switch (tp->type) {
      case TracepointType::Histogram:
         collection_entry.second.f += 1;
         break;
      case TracepointType::LineGraph:
      {
         auto field_value = get_field_value(fields_str, tp->field_name);
         if (!field_value.empty()) {
            char *value_end;
            uint64_t value = strtoull(field_value.c_str(), &value_end, 16);

            if (value != ULLONG_MAX)
               collection_entry.second.f += (float) value;
         }
        break;
      }
      case TracepointType::Label:
      {
         auto field_value = get_field_value(fields_str, tp->field_name);
         if (!field_value.empty())
            collection_entry.second.field_value = field_value;
         break;
      }
      default:
         UNREACHABLE("invalid tracepoint type");
      }
   }
}

void FTrace::update()
{
   auto update_index = ++data.update_index % Tracepoint::PLOT_DATA_CAPACITY;
   for (auto& tp : data.tracepoints) {
      tp->update_index = update_index;

      // Iterate over the stored plot values often enough to keep
      // the data range updated and plot presentation visually useful.
      if (!(update_index % (Tracepoint::PLOT_DATA_CAPACITY / 4))) {
         auto max = tp->data.plot.values[0];
         for (auto v : tp->data.plot.values)
            max = std::max(max, v);

         tp->data.plot.range = { 0, max };
      }
   }

   std::unique_lock<std::mutex> lock(collection.mutex);

   for (auto& it : collection.map) {
      auto& tp = it.first;
      auto& value = it.second;

      switch (tp->type) {
      case TracepointType::Histogram:
      case TracepointType::LineGraph:
      {
         tp->data.plot.values[tp->update_index] = value.f;
         auto max = std::max(tp->data.plot.range.max, value.f);
         tp->data.plot.range = { 0, max };
         break;
      }
      case TracepointType::Label:
      {
         if (!value.field_value.empty())
            tp->data.field_value = value.field_value;
         break;
      }
      default:
         UNREACHABLE("invalid tracepoint type");
      }

      value = { };
   }
}

float FTrace::get_plot_values(void *data, int index)
{
   const Tracepoint& tp = *((const Tracepoint *) data);
   return tp.data.plot.values[(index + tp.update_index + 1) % Tracepoint::PLOT_DATA_CAPACITY];
}

std::unique_ptr<FTrace> object = nullptr;

} // namespace FTrace

#endif // HAVE_FTRACE
