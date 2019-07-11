#include "config.h"

#ifdef HAVE_REGEX
using std::regex;
using std::smatch;
#else
using boost::regex;
using boost::smatch;
#endif

// #define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
// #if GCC_VERSION < 40900
// #include <boost/regex.hpp>
// using boost::regex;
// using boost::smatch;
// #else
// #include <regex>
// using std::regex;
// using std::smatch;
// #endif

#ifdef _WIN32
#include <process.h>
#endif // _WIN32

#include "FileEntity.h"         
//#include "VulnerableFileEntity.h" 
#include "PhonyEntity.h"          
#include "Obj2ExeAction.h"        
#include "Cpp2ObjAction.h"        
//#include "Cpp2DepAction.h"        
#include "H2GchAction.h"          
#include "UpdateGraphAction.h" 
#include "ShebangMagic.h"                
#include "GchMagic.h"                    
#include "helpers.h"                     
#include "Loggers.h"                     
#include "samples.h"                     

// 路径相关的变量命名约定：
// xxx_file 文件的相对路径，类型fs::path
// xxx_dir 目录的相对路径，类型fs::path
// xxx_file_path 文件的绝对路径，类型fs::path
// xxx_dir_path 目录的绝对路径，类型fs::path
// xxx_file_name 文件的相对路径或绝对路径，类型string
// xxx_dir_name 目录的相对路径或绝对路径，类型string
// 如果xxx部分足够清晰，可省略file与dir后缀。

//string gcc_compile_cpp_cmd = "g++ -std=c++11 -fmax-errors=2 -Wall -c"; // -fmax-errors=2是因为某些错误需要两条错误信息
//string gcc_compile_h_cmd = "g++ -std=c++11 -fmax-errors=2 -Wall";
//string gcc_compile_cpp_cmd = "g++ -std=c++11 -Wall -c"; // -fmax-errors=2是因为某些错误需要两条错误信息
string gcc_compile_cpp_cmd = "cl /nologo /EHsc /c";
string gcc_compile_h_cmd = "g++ -std=c++11 -Wall";

// 如果使用了<thread>或者<future>里的东西，就需要-pthread这个参数。注意，不是-lpthread，不过效果似乎是一样的。
#if defined(__CYGWIN__) || defined(_WIN32)
//string gcc_link_cmd = "g++ -std=c++11 -fmax-errors=1";
string gcc_link_cmd = "cl /nologo";
#else
string gcc_link_cmd = "g++ -std=c++11 -fmax-errors=1 -pthread";
#endif

// 搜集到的项目信息
fs::path exe_path;              // 生成的可执行文件的绝对路径
fs::path script_file;           // 命令行上指定的脚本路径（这是个相对路径）
vector<fs::path> sources; // 所有.cpp文件的绝对路径
vector<string> libs; // 库的名称，即链接时命令行上-l之后的部分
vector<fs::path> headers_to_pc; // 所有需要预编译的头文件的绝对路径
vector<fs::path> sources_to_pc; // 所有需要预编译的.cpp文件的绝对路径(vc的头文件不能直接编译，必须被#include在一个.cpp文件中编译。)
map<fs::path, fs::path> source2header_to_pc; // 预编译头文件跟对应的.cpp的对应关系
string extra_compile_flags; // 编译时用的其他选项
string extra_link_flags; // 链接时用的其他选项

// 命令行参数对应的变量
bool verbose = false;
bool collect_only = false;
bool build_only = false;
bool show_dep_graph = false;
string script_file_name;
string main_file_name;
string class_name;
bool clear_run = false;
int run_by = 1; // 0 - system(); 1 - execv()
int max_line_scan = -1;              // 最多扫描这么多行，-1代表全部扫描
string output_name;

void collect_info();
bool build_exe();
bool build();
void run(int argc, char* argv[]);
void generate_main_file(string main_file__name);
void generate_class_files(string class_name);
fs::path resolve_shebang_wrapper(fs::path wrapper_path);

char usage[] = "Usage: cpps [options] <script.cpp> [args]";
po::variables_map vm;

int script_pos; // 脚本在cpps命令行参数中的位置
int argc_script; // 脚本的参数个数

int main(int argc, char* argv[])
try {
    // 确定脚本在命令行上的位置script_pos
    // 调整cpps的argc，不让其看到脚本的命令行选项与参数
    for (int i = 0; i < argc; i++) {
        if (is_a_cpp_src(argv[i])) {
            script_pos = i;
            argc_script =  argc - i;
            argc = i + 1;
//            cout << "script_pos:" << script_pos << endl;
//            cout << "argc_script:" << argc_script << endl;
            break;
        }
    }

    // 处理命令行参数
    po::options_description info_opts("Information options");
    info_opts.add_options()
        ("help,h", "produce help message")
        ("verbose,v", po::bool_switch(&verbose), "be verbose")
        ("dependency,d", po::bool_switch(&show_dep_graph), "show dependency graph")
        ("collect", po::bool_switch(&collect_only), "only show information collected")
        ;

    po::options_description build_opts("Build options");
    build_opts.add_options()
        ("build", po::bool_switch(&build_only), "only build")
        ("output,o", po::value(&output_name), "output resulting exe as named")
        ("clear", po::bool_switch(&clear_run), "run within a clear environment")
        ("max-line-scan", po::value<int>(&max_line_scan), "scan up to N lines")
        ;

    po::options_description run_opts("Run options");
    run_opts.add_options()
        ("run-by,r", po::value<int>(&run_by)->default_value(1), "run by: 0 - system, 1 - execv")
        ;

    po::options_description generation_opts("Generation options");
    generation_opts.add_options()
        ("generate,g", po::value(&main_file_name), "generate a script skeleton")
        ("class,c", po::value(&class_name), "generate .h/.cpp pair for a class")
        ;

    po::options_description config_opts("Configuration options");
    config_opts.add_options()
        ("include-dir,I", po::value<vector<string>>(), "add a directory to be searched for header files")
        ("lib-dir,L", po::value<vector<string>>(), "add a directory to be searched for libs")
        ;

    po::options_description hidden_opts("Hidden options");
    hidden_opts.add_options()
        ("script", po::value(&script_file_name), ".cpp file including int main()")
        ("args", po::value<vector<string>>(), "args being passed to the script")
        ;

    po::options_description cmdline_options; // 用于解析命令的选项
    cmdline_options.add(info_opts).add(build_opts).add(run_opts).add(generation_opts).add(config_opts).add(hidden_opts);

    po::options_description visible_options; // 呈现给用户的选项
    visible_options.add(info_opts).add(build_opts).add(run_opts).add(generation_opts).add(config_opts);

    po::positional_options_description p;
    p.add("script", 1);
    p.add("args", -1);

    po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).allow_unregistered().run(), vm);
    po::notify(vm);

    // 加载配置文件
    fs::path cfg_path = get_home();
    //cfg_path /= ".cpps/config.txt";
	cfg_path /= ".cpps/vconfig.txt";
    ifstream ifs(cfg_path.string());
    if (ifs) {
        po::store(parse_config_file(ifs, config_opts), vm);
        po::notify(vm);
    }

    if (vm.count("help")) {
        cout << usage << endl;
        cout << visible_options << "\n";
        return 0;
    }

    if (vm.count("generate")) {
        generate_main_file(main_file_name);
        return 0;
    }

    if (vm.count("class")) {
        generate_class_files(class_name);
        return 0;
    }

    if (vm.count("script") == 0) {
        cout << usage << endl;
        return 0;
    }

    if (verbose) {
        build_gch_summay_logger.enable();
        build_exe_summay_logger.enable();
        perm_logger.enable();
    }

    if (collect_only) {
        collect_info_logger.enable();
    }

    script_file = script_file_name;

    if (!exists(script_file)) {
        cout << "No such file.\n";
        return 1;
    }

    if (extension(script_file) == ".cpps") {
        script_file = resolve_shebang_wrapper(script_file);
        MINILOG(shebang_logger, "resolved to " << script_file);
    }

    if (!is_a_cpp_src(script_file) && !is_a_c_src(script_file)) {
        cout << script_file << " should have a C/C++ suffix." << endl;
        return 1;
    }

    if (vm.count("include-dir")) {
        for (auto dir : vm["include-dir"].as<vector<string>>()) {
            gcc_compile_cpp_cmd += " -I";
            gcc_compile_cpp_cmd += dir;
            //cout << a << endl;
        }
    }

    if (vm.count("lib-dir")) {
        for (auto dir : vm["lib-dir"].as<vector<string>>()) {
            gcc_link_cmd += " -L";
            gcc_link_cmd += dir;
            //cout << a << endl;
        }
    }

    // 搜集信息
    collect_info();
    if (collect_only)
        return 0;

    gcc_compile_cpp_cmd += extra_compile_flags;
    gcc_compile_h_cmd += extra_compile_flags;
    //gcc_link_cmd += extra_link_flags;

    // 构建
    bool success = build();
    if (build_only)
        return 0;

    if (success) {
        // 运行
        run(argc, argv);
    }


    return 0;
}
catch (int exit_code) {
    return exit_code;
}

bool is_one_of(fs::path file, const vector<fs::path>& file_set)
{
	if (std::find(std::begin(file_set), std::end(file_set), file) == std::end(file_set)) {
		return false;
	}

	return true;
}

void make_sure_these_at_the_head(vector<fs::path>& sources_to_pc, vector<fs::path>& sources)
{
	vector<fs::path> original_sources{ sources };
	sources.clear();
	sources = sources_to_pc;

	for (auto src : original_sources) {
		if (!is_one_of(src, sources_to_pc)) {
			sources.push_back(src);
		}
		
	}
}

bool build_exe()
{
    // 构建依赖关系图
    FileEntityPtr exe = makeFileEntity(exe_path);
    string lib_options;
    for (auto l : libs) {
        lib_options += " -l";
        lib_options += l;
    }
    exe->addAction(makeObj2ExeAction(lib_options));

    PhonyEntityPtr update_dependency = makePhonyEntity("update dependency graph");


	// 将sources中跟预编译头文件相关的.cpp提到到最前边，以便先行编译。
	make_sure_these_at_the_head(sources_to_pc, sources);
	assert(sources_to_pc.size() <= 1);
	
    for (auto src_path : sources) {

        // 根据.cpp文件的名字，确定.o文件的名字
        fs::path obj_path = shadow(src_path);
        //obj_path += ".o";
		obj_path += ".obj";

        //
        //FileEntityPtr obj = makeVulnerableFileEntity(obj_path);

		string additional_options = "";
		fs::path h_path = headers_to_pc[0];
		if (headers_to_pc.empty()) { // 如果压根没有预编译头文件

		}
		else {
			//fs::path h_path = source2header_to_pc[src_path];
			//fs::path h_path = headers_to_pc[0];
			fs::path pch_path = shadow(h_path);
			pch_path += ".pch";

			additional_options += "/Fp: ";
			additional_options += pch_path.string();
			if (is_one_of(src_path, sources_to_pc)) { // 用于生成预编译头文件的
				additional_options += " /Yc";
			}
			else { // 使用预编译头文件的
				additional_options += " /Yu";
			}
			additional_options += h_path.filename().string();
		}

		FileEntityPtr obj = makeFileEntity(obj_path);
		obj->addAction(makeCpp2ObjAction(additional_options, h_path));
        

        // 可执行文件依赖.o文件
        exe->addPrerequisite(obj);

        // .o文件依赖.cpp文件
        FileEntityPtr src = makeFileEntity(src_path);
        obj->addPrerequisite(src);

        // 根据.o文件的名字，确定.d文件的名字
        fs::path dep_path = obj_path;
        dep_path += ".d";

        if (exists(dep_path)) {
            PhonyEntityPtr obj_update = makePhonyEntity("update for " + obj_path.string());
            obj_update->addAction(makeUpdateGraphAction(obj));
            update_dependency->addPrerequisite(obj_update);

            FileEntityPtr dep = makeFileEntity(dep_path);
            obj_update->addPrerequisite(dep);

            // 其实dep文件也是易变的，但我们这会为简便起见，就当它不会变
            // dep->addPrerequisite(src);
            // dep->addAction(makeCpp2DepAction());
        }


    } // for

    // 根据依赖关系图进行构建

    MINILOGBLK_IF(
        show_dep_graph, dep_graph_logger,
        os << endl;
        update_dependency->show(os);
        os << endl;
        );

    update_dependency->update();

    MINILOGBLK_IF(
        show_dep_graph, dep_graph_logger,
        exe->show(os);
        os << endl;
        );

    bool success = exe->update();
    if (!success)
        return false;

    if (vm.count("output")) {
        fs::path output_path = output_name;
        if (!exists(output_path)) {
            copy(exe_path, output_path);
        }
        else if (!is_directory(output_path)) {
            remove(output_path);
            copy(exe_path, output_path);
        }
        else {
            cout << "a directory named " << output_name << " already exists" << endl;
        }

    }
    return true;
}

bool build_gch()
{
	assert(false); // vc没有专门的编译头文件的过程

    if (headers_to_pc.empty())
        return true;

    PhonyEntityPtr all_gch  = makePhonyEntity("generate all gch");
    PhonyEntityPtr update_dependency = makePhonyEntity("update dependency graph");

    for (auto header_path : headers_to_pc) {
        fs::path gch_path = header_path;
        gch_path += ".gch";



        //FileEntityPtr gch = makeVulnerableFileEntity(gch_path);
        FileEntityPtr gch = makeFileEntity(gch_path);
        gch->addAction(makeH2GchAction());

        //
        all_gch->addPrerequisite(gch);

        // .gch文件依赖.h文件
        FileEntityPtr header = makeFileEntity(header_path);
        gch->addPrerequisite(header);

        // 根据.gch文件的名字，确定.d文件的名字
        fs::path dep_path = shadow(gch_path);
        dep_path += ".d";

        if (exists(dep_path)) {
            PhonyEntityPtr gch_update = makePhonyEntity("update for " + gch_path.string());
            gch_update->addAction(makeUpdateGraphAction(gch));
            update_dependency->addPrerequisite(gch_update);

            FileEntityPtr dep = makeFileEntity(dep_path);
            gch_update->addPrerequisite(dep);
        }
    } // for

    // 根据依赖关系图进行构建
    MINILOGBLK_IF(
        show_dep_graph, dep_graph_logger,
        os << endl;
        update_dependency->show(os);
        os << endl;
        );

    update_dependency->update();

    MINILOGBLK_IF(
        show_dep_graph, dep_graph_logger,
        all_gch->show(os);
        os << endl;
        );

    bool success;
    success = all_gch->update();
    if (!success)
        return false;

    return true;
}

bool build()
{
    if (clear_run) {
        // 在build前删除所有的产生的文件
        safe_remove(exe_path);

        for (auto src : sources) {
            fs::path obj_path = shadow(src);
            //obj_path += ".o";
			obj_path += ".obj";
            safe_remove(obj_path);

            fs::path dep_path = obj_path;
            dep_path += ".d";
            safe_remove(dep_path);

            fs::path bc_path = obj_path;
            bc_path += ".birthcert";
            safe_remove(bc_path);
        }

        for (auto h : headers_to_pc) {
            fs::path gch_path = shadow(h);
            //gch_path += ".gch";
			gch_path += ".pch";
            safe_remove(gch_path);

            fs::path dep_path = gch_path;
            dep_path += ".d";
            safe_remove(dep_path);

            fs::path bc_path = gch_path;
            bc_path += ".birthcert";
            safe_remove(bc_path);
        }
    }

    bool success;

	// vc没有专门的编译头文件的过程，所以将一下几行注释掉。
    //GchMagic gch_magic(headers_to_pc);
    //success = build_gch();
    //if (!success)
    //    return false;

    ShebangMagic shebang_magic(script_file.string());
    success = build_exe();
    if (!success)
        return false;

    return true;
}

void scan(fs::path src_path)
{
    // 如果已经处理过该文件，就不要再处理，避免循环引用
    if (find(sources.begin(), sources.end(), src_path) != sources.end()) {
        MINILOG(collect_info_logger, src_path << " is already collected.");
        return;
    }

    MINILOG(collect_info_logger, "scanning " << src_path);

    sources.push_back(src_path);

    ifstream in(src_path.string());

    string line;
    regex usingcpp_pat {R"(^\s*#include\s+"([\w\./]+)\.h"\s+//\s+usingcpp)"};
    regex using_pat {R"(using\s+([\w\./]+\.(c|cpp|cxx|c\+\+|C|cc|cp|CPP)))"};
    regex linklib_pat {R"(linklib\s+([\w\-\.]+))"};
    regex precompile_pat {R"***(^\s*#include\s+"([\w\./]+\.(h|hpp|H|hh))"\s+//\s+precompile)***"};
    regex extra_compile_flags_pat {R"(extra-compile-flags:\s+(.*)$)"};
    regex extra_link_flags_pat {R"(extra-link-flags:\s+(.*)$)"};
    int n = 0;
    while (getline(in,line)) {
        smatch matches;
        // 搜集引用的.cpp文件
        if (regex_search(line, matches, usingcpp_pat)) {
            string cpp_file_name = matches[1];
            cpp_file_name += ".cpp";
            MINILOG(collect_info_logger, "found: " << cpp_file_name);
            // 一个.cpp文件中引用的另一个.cpp文件，是以相对路径的形式指明的
            fs::path a = src_path;
            a.remove_filename();
            a /= cpp_file_name;
            if (exists(a)) {
                // 递归处理
                scan(a);
            }
            else {
                cout << a << " referenced by " << src_path << " does NOT exsit!"<< endl;
                throw 1;
            }
        }
        else if (regex_search(line, matches, using_pat)) {
            MINILOG(collect_info_logger, "found: " << matches[1]);
            // 一个.cpp文件中引用的另一个.cpp文件，是以相对路径的形式指明的
            fs::path a = src_path;
            a.remove_filename();
            a /= matches.str(1);
            if (exists(a)) {
                // 递归处理
                scan(a);
            }
            else {
                cout << a << " referenced by " << src_path << " does NOT exsit!"<< endl;
                throw 1;
            }
        }

        // 搜集使用的库名字
        if (regex_search(line, matches, linklib_pat)) {
            MINILOG(collect_info_logger, "found lib: " << matches[1]);
            libs.push_back(matches[1]);
        }

        if (regex_search(line, matches, extra_compile_flags_pat)) {
            MINILOG(collect_info_logger, "found extra compile flags: " << matches[1]);
            extra_compile_flags += " ";
            extra_compile_flags += matches[1];
        }

        if (regex_search(line, matches, extra_link_flags_pat)) {
            MINILOG(collect_info_logger, "found extra link flags: " << matches[1]);
            extra_link_flags += " ";
            extra_link_flags += matches[1];
        }

        // 搜集需要预编译的头文件
        if (regex_search(line, matches, precompile_pat)) {
            MINILOG(collect_info_logger, "found pch: " << matches[1]);
            // 一个.cpp文件要求预编译某个头文件，是以相对路径的形式指明的
            fs::path a = src_path;
            a.remove_filename();
            a /= matches.str(1);
            if (exists(a)) {
                if (find(headers_to_pc.begin(), headers_to_pc.end(), a) != headers_to_pc.end()) {
                    MINILOG(collect_info_logger, a << " is already collected.");
                    return;
                }
                headers_to_pc.push_back(a);
				sources_to_pc.push_back(src_path);
				source2header_to_pc[src_path] = a;
				if (headers_to_pc.size() >= 2) {
					cout << "at the moment, vcpps supports only one precompiled header, but many found:" << endl;
					for (auto h : headers_to_pc) {
						cout << h.filename() << endl;
					}
					throw 1;
				}
            }
            else {
                cout << a << " referenced by " << src_path << " does NOT exsit!"<< endl;
                throw 1;
            }
        }


        if (max_line_scan != -1) {
            n++;
            if (n >= max_line_scan)
                break;
        }

    }

    MINILOG(collect_info_logger, "scanning " << src_path << " completed");

}

void collect_info()
{
    // 确定可执行文件的路径
    fs::path script_path = canonical(script_file).make_preferred();
    exe_path = shadow(script_path);
    exe_path += ".exe";

    // 确定所有.cpp文件的路径；确定所有库的名字；确定所有的预编译头文件的路径
    scan(canonical(script_path).make_preferred());
//    cout << "***" << script_file << endl;
//    cout << "***" << script_path.make_preferred() << endl;
//    cout << "***" << canonical(script_path) << endl;

}

void run(int argc, char* argv[])
{
//    const int max_num_of_args = 100;
//    const int max_len_of_arg = 100;
//    char script_arg_vector[max_num_of_args][max_len_of_arg];
//    char* script_argv[max_num_of_args];
//    int script_argc;


#ifdef _WIN32
    //run_by = 0;
#endif // _WIN32

    if (run_by == 1) {
        MINILOG0("run using execv()");

//        for (int i = 0; i < max_num_of_args; i++) {
//            script_argv[i] = script_arg_vector[i];
//            script_arg_vector[i][0] = '\0';
//        }
//        script_argc = 0;
//
//        strcpy(script_argv[0], script_file.string().c_str());
//        script_argc++;

//        if (vm.count("args")) {
//            for (auto a : vm["args"].as<vector<string>>()) {
//                strcpy(script_arg_vector[script_argc++], a.c_str());
//            }
//        }
//        script_argv[script_argc] = 0;

#ifdef _WIN32
        // Windows没有exec功能，但有个_exec函数来模拟exec。
        // 可是_exec创建了一个子进程，往往子进程还没有结束，父进程就已经结束了。
        // 导致命令行窗口的输出混乱。
        //_execv(exe_path.string().c_str(), script_argv);
        //_spawnv(_P_WAIT, exe_path.string().c_str(), script_argv);
        _spawnv(_P_WAIT, exe_path.string().c_str(), argv + script_pos);
#else
        //execv(exe_path.c_str(), script_argv);
        execv(exe_path.c_str(), argv + script_pos);
#endif
    }
    else if (run_by == 0) {
        MINILOG0("run using system()");
        string script_args;

//        if (vm.count("args")) {
//            for (auto a : vm["args"].as<vector<string>>()) {
//                script_args += " '"; // 把脚本的参数用单引号括起来，避免通配符展开。该展开的通配符在解释器执行时已经展开过了
//                script_args += a;
//                script_args += "'";
//            }
//        }

        for (int i = 1; i < argc_script; i++) {
                script_args += " '"; // 把脚本的参数用单引号括起来，避免通配符展开。该展开的通配符在解释器执行时已经展开过了
                script_args += argv[script_pos + i];
                script_args += "'";
        }

        string cmd_line= exe_path.string();
        cmd_line += script_args;
        MINILOG0("final cmd line: " << cmd_line);
        // 运行产生的可执行文件
        system(cmd_line.c_str());
    }

}

void generate_main_file(string main_file_name)
{
    ofstream f;

    if (fs::exists(main_file_name)) {
        cout << main_file_name << " already exists." << endl;
        return;
    }
    f.open(main_file_name);
    f << main_sample;
    f.close();


    if (fs::exists("std.h")) {
        cout << "std.h already exists." << endl;
        return;
    }
    f.open("std.h");
    f << std_header_sample;
    f.close();
}

void generate_class_files(string class_name)
{
    string h_name = class_name + ".h";
    string cpp_name = class_name + ".cpp";

    ofstream f;
    regex pat;
    string format;

    if (fs::exists(h_name)) {
        cout << h_name << " already exists." << endl;
        return;
    }
    f.open(h_name);
    pat = "XXX_H";
    format = al::to_upper_copy(class_name) + "_H";
    string h_content = regex_replace(string(class_h_sample), pat, format);

    pat = "XXX";
    format = class_name;
    h_content = regex_replace(h_content, pat, format);
    f << h_content;
    f.close();


    if (fs::exists(cpp_name)) {
        cout << cpp_name << " already exists." << endl;
        return;
    }
    f.open(cpp_name);
    pat = "XXX";
    format = class_name;
    string cpp_content = regex_replace(string(class_cpp_sample), pat, format);
    f << cpp_content;
    f.close();
}

fs::path resolve_shebang_wrapper(fs::path wrapper_path)
{
    // 得到实际脚本的路径
    ifstream in(wrapper_path.string());
    string firstline, secondline;
    getline(in, firstline);
    getline(in, secondline);
    fs::path script_path(secondline);


    if (!script_path.is_absolute()) { // 如果该路径是个相对路径，根据wrapper的路径进行拼装
        script_path = wrapper_path.parent_path();;
        script_path /= secondline;
    }

    if (!exists(script_path)) {
        cout << script_path << " referenced by " << wrapper_path << " does NOT exist." << endl;
        throw -1;
    }

    return script_path;
}
