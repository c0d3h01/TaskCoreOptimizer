/*
 * Core Task Optimizer 
 * Version: v1.1.3
 * 
 * A lightweight system optimization tool for Linux systems that enhances
 * performance through task prioritization and resource allocation.
 * 
 * MIT License
 *
 * Copyright (c) 2024 c0d3h01
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <memory>
#include <stdexcept>
#include <regex>
#include <fstream>
#include <filesystem>
#include <sstream>

using namespace std;

// Dirs for logs.
const string LOG_DIR = "/data/adb/modules/task_optimizer/logs/";
const string LAUNCHER_LOG = LOG_DIR + "launcher_package.log";
const string AFFINITY_LOG = LOG_DIR + "affinity.log";
const string RT_LOG = LOG_DIR + "rt.log";
const string CGROUP_LOG = LOG_DIR + "cgroup.log";
const string NICE_LOG = LOG_DIR + "nice.log";
const string IOPRIO_LOG = LOG_DIR + "ioprio.log";
const string IRQ_LOG = LOG_DIR + "irqaffinity.log";

// Function_declarations.
void unpinThread(const string& taskName, const string& threadName);
void unpinProc(const string& taskName);
void pinThreadOnCpus(const string& taskName, const string& threadName, const string& cpus);
void pinProcOnCpus(const string& taskName, const string& cpus);

// Error_logging.
void logError(const string& message, const string& logFile) {
    ofstream log(logFile, ios::app);
    if (log.is_open()) {
        log << "[ERROR] " << message << endl;
        log.close();
    } else {
        cerr << "[ERROR] Cannot write to log file: " << logFile << endl;
    }
}

// Execute a shell command and log the output .
string executeCommand(const string& command, const string& logFilePath) {  
    string result;
    ofstream logFile(logFilePath, ios::app);
    
    if (logFile.is_open()) { 
        logFile << "Executing command: " << command << endl; 
        
        FILE* pipe = popen(command.c_str(), "r");
        if (pipe) {
            char buffer[128];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result += buffer;
            }
            int status = pclose(pipe);
            logFile << "Command execution result: " << status << endl;
            logFile << "Output: " << result << endl;
        } else {
            logFile << "Failed to execute command" << endl;
        }
        logFile.close();
    } else {
        cerr << "Unable to open log file: " << logFilePath << endl;
    }
    
    return result;
}

// Helper function to get the process IDs for a given task name.
vector<pid_t> getProcessIDs(const string& taskName) {
    vector<pid_t> pids;
    string command = "pgrep -f '" + taskName + "'";
    FILE* pipe = popen(command.c_str(), "r");

    if (pipe) {
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            pid_t pid = atoi(buffer);
            pids.push_back(pid);
        }
        pclose(pipe);
    }

    return pids;
}

// Helper function to get the thread IDs for a given process ID.
vector<pid_t> getThreadIDs(pid_t pid) {
    vector<pid_t> tids;
    string taskDir = "/proc/" + to_string(pid) + "/task";
    DIR* dir = opendir(taskDir.c_str());
    
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir))) {
            if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                tids.push_back(strtol(entry->d_name, nullptr, 10));
            }
        }
        closedir(dir);
    }

    return tids;
}

// Function to change task cgroup.
void changeTaskCgroup(const string& taskName, const string& cgroupName, const string& cgroupType) {
    vector<pid_t> pids = getProcessIDs(taskName);
    for (pid_t pid : pids) {
        vector<pid_t> tids = getThreadIDs(pid);
        for (pid_t tid : tids) {
            string command = "echo " + to_string(tid) + " > /dev/" + cgroupType + "/" + cgroupName + "/tasks";
            executeCommand(command, "/data/adb/modules/task_optimizer/logs/cgroup.log");
        }
    }
}

// Function to change thread cgroup.
void changeThreadCgroup(const string& taskName, const string& threadName, const string& cgroupName, const string& cgroupType) {
    vector<pid_t> pids = getProcessIDs(taskName);
    for (pid_t pid : pids) {
        vector<pid_t> tids = getThreadIDs(pid);
        for (pid_t tid : tids) {
            string command = "cat /proc/" + to_string(pid) + "/task/" + to_string(tid) + "/comm";
            FILE* pipe = popen(command.c_str(), "r");
            if (pipe) {
                char buffer[128];
                if (fgets(buffer, sizeof(buffer), pipe)) {
                    string comm(buffer);
                    if (comm.find(threadName) != string::npos) {
                        string cgroupCommand = "echo " + to_string(tid) + " > /dev/" + cgroupType + "/" + cgroupName + "/tasks";
                        executeCommand(cgroupCommand, "/data/adb/modules/task_optimizer/logs/cgroup.log");
                    }
                }
                pclose(pipe);
            }
        }
    }
}

// Function to change thread affinity.
void changeThreadAffinity(const string& taskName, const string& threadName, const string& hexMask) {
    vector<pid_t> pids = getProcessIDs(taskName);
    for (pid_t pid : pids) {
        vector<pid_t> tids = getThreadIDs(pid);
        for (pid_t tid : tids) {
            string command = "cat /proc/" + to_string(pid) + "/task/" + to_string(tid) + "/comm";
            FILE* pipe = popen(command.c_str(), "r");
            if (pipe) {
                char buffer[128];
                if (fgets(buffer, sizeof(buffer), pipe)) {
                    string comm(buffer);
                    if (comm.find(threadName) != string::npos) {
                        string affinityCommand = "taskset -p " + hexMask + " " + to_string(tid);
                        executeCommand(affinityCommand, "/data/adb/modules/task_optimizer/logs/affinity.log");
                    }
                }
                pclose(pipe);
            }
        }
    }
}

// Function to change task affinity.
void changeTaskAffinity(const string& taskName, const string& hexMask) {
    vector<pid_t> pids = getProcessIDs(taskName);
    for (pid_t pid : pids) {
        vector<pid_t> tids = getThreadIDs(pid);
        for (pid_t tid : tids) {
            string affinityCommand = "taskset -p " + hexMask + " " + to_string(tid);
            executeCommand(affinityCommand, "/data/adb/modules/task_optimizer/logs/affinity.log");
        }
    }
}

// Function to change task nice value.
void changeTaskNice(const string& taskName, int niceVal) {
    vector<pid_t> pids = getProcessIDs(taskName);
    for (pid_t pid : pids) {
        vector<pid_t> tids = getThreadIDs(pid);
        for (pid_t tid : tids) {
            string niceCommand = "renice -n " + to_string(niceVal) + " -p " + to_string(tid);
            executeCommand(niceCommand, "/data/adb/modules/task_optimizer/logs/nice.log");
        }
    }
}

// Function to change thread nice value.
void changeThreadNice(const string& taskName, const string& threadName, int niceVal) {
    vector<pid_t> pids = getProcessIDs(taskName);
    for (pid_t pid : pids) {
        vector<pid_t> tids = getThreadIDs(pid);
        for (pid_t tid : tids) {
            string command = "cat /proc/" + to_string(pid) + "/task/" + to_string(tid) + "/comm";
            FILE* pipe = popen(command.c_str(), "r");
            if (pipe) {
                char buffer[128];
                if (fgets(buffer, sizeof(buffer), pipe)) {
                    string comm(buffer);
                    if (comm.find(threadName) != string::npos) {
                        string niceCommand = "renice -n " + to_string(niceVal) + " -p " + to_string(tid);
                        executeCommand(niceCommand, "/data/adb/modules/task_optimizer/logs/nice.log");
                    }
                }
                pclose(pipe);
            }
        }
    }
}

// Function to change task real-time priority.
void changeTaskRt(const string& taskName, int priority) {
    vector<pid_t> pids = getProcessIDs(taskName);
    for (pid_t pid : pids) {
        vector<pid_t> tids = getThreadIDs(pid);
        for (pid_t tid : tids) {
            string rtCommand = "chrt -p " + to_string(priority) + " " + to_string(tid);
            executeCommand(rtCommand, "/data/adb/modules/task_optimizer/logs/rt.log");
        }
    }
}

// Function to change task I/O priority.
void changeTaskIoPrio(const string& taskName, int classType, int classLevel) {
    vector<pid_t> pids = getProcessIDs(taskName);
    for (pid_t pid : pids) {
        vector<pid_t> tids = getThreadIDs(pid);
        for (pid_t tid : tids) {
            string ioCommand = "ionice -c " + to_string(classType) + " -n " + to_string(classLevel) + " -p " + to_string(tid);
            executeCommand(ioCommand, "/data/adb/modules/task_optimizer/logs/ioprio.log");
        }
    }
}

// Function to change IRQ affinity.
void changeIrqAffinity(const string& taskName, const string& hexMask) {
    string command = "grep -r '" + taskName + "' /proc/interrupts | grep -oE '^\\\\[[:digit:]]\\\\+' | sort -u";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe) {
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            string irq(buffer);
            irq.erase(remove(irq.begin(), irq.end(), '\n'), irq.end());
            string affinityCommand = "echo " + hexMask + " > /proc/irq/" + irq + "/smp_affinity";
            executeCommand(affinityCommand, "/data/adb/modules/task_optimizer/logs/irqaffinity.log");
        }
        pclose(pipe);
    }
}

// Function to unpin thread from CPU affinity.
void unpinThread(const string& taskName, const string& threadName) {
    changeThreadCgroup(taskName, threadName, "", "cpuset");
}

// Function to unpin process from CPU affinity.
void unpinProc(const string& taskName) {
    changeTaskCgroup(taskName, "", "cpuset");
}

// Function to pin thread on specific CPUs.
void pinThreadOnCpus(const string& taskName, const string& threadName, const string& cpus) {
    unpinThread(taskName, threadName);
    changeThreadAffinity(taskName, threadName, cpus);
}

// Function to pin process on specific CPUs.
void pinProcOnCpus(const string& taskName, const string& cpus) {
    unpinProc(taskName);
    changeTaskAffinity(taskName, cpus);
}

// Function to pin only the first threads matching the patternnn.
void pinProcOnFirst(const string& taskPattern, const string& hexMaskPrime, const string& hexMaskOthers) {
    vector<pid_t> pids = getProcessIDs(taskPattern);
    for (pid_t pid : pids) {
        bool firstThreadPinned = false;
        vector<pid_t> tids = getThreadIDs(pid);
        for (pid_t tid : tids) {
            string command = "cat /proc/" + to_string(pid) + "/task/" + to_string(tid) + "/comm";
            FILE* pipe = popen(command.c_str(), "r");
            if (pipe) {
                char buffer[128];
                if (fgets(buffer, sizeof(buffer), pipe)) {
                    string comm(buffer);
                    if (regex_search(comm, regex(taskPattern))) {
                        if (!firstThreadPinned) {
                            string affinityCommand = "taskset -p " + hexMaskPrime + " " + to_string(tid);
                            executeCommand(affinityCommand, "/data/adb/modules/task_optimizer/logs/affinity.log");
                            firstThreadPinned = true;
                        } else {
                            string affinityCommand = "taskset -p " + hexMaskOthers + " " + to_string(tid);
                            executeCommand(affinityCommand, "/data/adb/modules/task_optimizer/logs/affinity.log");
                        }
                    }
                }
                pclose(pipe);
            }
        }
    }
}

// Function to change task priority to RT, with a specific nice value.
void changeTaskRtFf(const string& taskName, int niceVal) {
    vector<pid_t> pids = getProcessIDs(taskName);
    for (pid_t pid : pids) {
        vector<pid_t> tids = getThreadIDs(pid);
        for (pid_t tid : tids) {
            string rtCommand = "chrt -p -f " + to_string(niceVal) + " " + to_string(tid);
            executeCommand(rtCommand, "/data/adb/modules/task_optimizer/logs/rt.log");
        }
    }
}

// Function to change task priority to high.
void changeTaskHighPrio(const string& taskName) {
    changeTaskNice(taskName, -20);
}

// Function to change task priority to RT, with idle priority.
void changeTaskRtIdle(const string& taskName) {
    changeTaskRt(taskName, 0);
}
int main() {
    try {
        // Create log directory
        filesystem::create_directories("/data/adb/modules/task_optimizer/logs/");
        
        // Set intent action and category
        string INTENT_ACTION = "android.intent.action.MAIN";
        string INTENT_CATEGORY = "android.intent.category.HOME";

        // Get launcher package name
        string command = "pm resolve-activity -a \"" + INTENT_ACTION + "\" -c \"" + INTENT_CATEGORY + "\" | grep packageName | head -1 | cut -d= -f2";
        string LAUNCHER_PACKAGE = executeCommand(command, LAUNCHER_LOG);
        LAUNCHER_PACKAGE = LAUNCHER_PACKAGE.substr(0, LAUNCHER_PACKAGE.find('\n'));

        // Keep your existing vector definitions
        vector<string> TASK_NAMES_HIGH_PRIO = {
            "servicemanag", "zygote", "writeback", "kblockd", "rcu_tasks_kthre", "ufs_clk_gating",
            "mmc_clk_gate", "system", "kverityd", "speedup_resume_wq", "load_tp_fw_wq", "tcm_freq_hop",
            "touch_delta_wq", "tp_async", "wakeup_clk_wq", "thread_fence", "Input"
        };

        vector<string> TASK_NAMES_LOW_PRIO = {"ipawq", "iparepwq", "wlan_logging_th"};

        vector<string> TASK_NAMES_RT_FF = {
            "kgsl_worker_thread", "devfreq_boost", "mali_jd_thread", "mali_event_thread", "crtc_commit",
            "crtc_event", "pp_event", "rot_commitq_", "rot_doneq_", "rot_fenceq_", "system_server",
            "surfaceflinger", "composer", "fts_wq", "nvt_ts_work"
        };

        vector<string> TASK_NAMES_RT_IDLE = {"f2fs_gc"};
        vector<string> TASK_NAMES_IO_PRIO = {"f2fs_gc"};

        // Input dispatcher/reader
        for (const string& taskName : TASK_NAMES_HIGH_PRIO) {
            changeTaskHighPrio(taskName);
        }

        // Render thread
        pinThreadOnCpus(LAUNCHER_PACKAGE, "RenderThread|GLThread", "ff");
        pinThreadOnCpus(LAUNCHER_PACKAGE, 
                      "GPU completion|HWC release|hwui|FramePolicy|ScrollPolicy|ged-swd", 
                      "0f");

        // Not important
        for (const string& taskName : TASK_NAMES_LOW_PRIO) {
            changeTaskNice(taskName, 0);
        }

        // Graphics workers are prioritized to run on perf cores
        changeIrqAffinity("msm_drm|fts", "80");
        changeIrqAffinity("kgsl_3d0_irq", "70");

        for (const string& taskName : TASK_NAMES_RT_FF) {
            changeTaskHighPrio(taskName);
            pinProcOnCpus(taskName, "f0");
            changeTaskRt(taskName, 50);
        }

        // SF should have all cores
        pinProcOnCpus("surfaceflinger", "ff");

        // Render threads
        // Pin only the first threads to prime, others are non-critical
        pinProcOnFirst("crtc_event:", "80", "0f");
        pinProcOnFirst("crtc_commit:", "80", "0f");
        pinProcOnCpus("pp_event", "80");
        pinProcOnCpus("mdss_fb|mdss_disp_wake|vsync_retire_work|pq@", "70");

        // TS workqueues on perf cluster to reduce latency
        pinProcOnCpus("fts_wq|nvt_ts_work", "70");
        changeTaskRtFf("fts_wq|nvt_ts_work", 50);

        // Samsung HyperHAL to perf cluster
        pinProcOnCpus("hyper@", "70");

        // CVP fence request handler
        changeTaskHighPrio("thread_fence");

        // RT priority adequately for critical tasks
        changeTaskRtFf("kgsl_worker_thread|crtc_commit|crtc_event|pp_event", 16);
        changeTaskRtFf("rot_commitq_|rot_doneq_|rot_fenceq_", 5);
        changeTaskRtFf("system_server|surfaceflinger|composer", 2);

        // GC should run conservatively as possible to reduce latency spikes
        for (const string& taskName : TASK_NAMES_RT_IDLE) {
            changeTaskRtIdle(taskName);
        }

        for (const string& taskName : TASK_NAMES_IO_PRIO) {
            changeTaskIoPrio(taskName, 3, 0);
        }

    } catch (const exception& e) {
        logError(string("Exception occurred: ") + e.what(), LOG_DIR + "error.log");
        return 1;
    }

    return 0;
}