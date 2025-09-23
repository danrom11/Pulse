#include <pulse/pulse.hpp>
#include <iostream>
#include <string>

using namespace pulse;

struct FileSaved { std::string path; };

int main() {
  topic<FileSaved> files;

  inline_executor ui;
  strand queue;

  auto s1 = files.subscribe(ui, priority{10}, bp_none{}, [](const FileSaved& e){
    std::cout << "[UI]     " << e.path << "\n";
  });

  auto s2 = files.subscribe(ui, priority{5}, bp_drop{2}, [](const FileSaved& e){
    std::cout << "[DROP]   " << e.path << "\n";
  });

  auto s3 = files.subscribe(queue, priority{1}, bp_latest<FileSaved>{}, [](const FileSaved& e){
    std::cout << "[LATEST] " << e.path << "\n";
  });

  auto s4 = files.subscribe(queue, priority{2}, bp_buffer_n<FileSaved, 3>{}, [](const FileSaved& e){
    std::cout << "[BUFFER] " << e.path << "\n";
  });

  auto s5 = files.subscribe(queue, priority{3}, bp_batch_n<FileSaved, 2>{}, [](const FileSaved& e){
    std::cout << "[BATCH2] " << e.path << "\n";
  });

  auto s6 = files.subscribe(
    queue,
    priority{4},
    bp_batch_count_or_timeout_nms<FileSaved, 2, 50>{},
    [](const FileSaved& e){ std::cout << "[BATCH2|50ms] " << e.path << "\n"; }
  );

  // --- reactive part: observable pipeline on top of topic ---
  auto stream = as_observable(files, ui)
    | map([](const FileSaved& e){ return e.path; })
    | filter([](const std::string& p){ return p.size() >= 4 && p.rfind(".png") == p.size()-4; })
    | observe_on(ui);

  auto s7 = stream.subscribe([](const std::string& p){
    std::cout << "[PIPE]   " << p << "\n";
  });


  for (int i = 0; i < 5; ++i) {
    files.publish(FileSaved{"/tmp/file" + std::to_string(i) + ".png"});
  }

  queue.drain();
}
