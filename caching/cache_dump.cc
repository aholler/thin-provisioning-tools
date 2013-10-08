#include <fstream>
#include <getopt.h>
#include <libgen.h>
#include <iostream>

#include "version.h"
#include "caching/mapping_array.h"
#include "caching/metadata.h"
#include "caching/xml_format.h"
#include "persistent-data/file_utils.h"

using namespace std;
using namespace caching;
using namespace caching::mapping_array_damage;
using namespace caching::superblock_damage;

//----------------------------------------------------------------

namespace {
	struct flags {
		flags()
			: repair_(false) {
		}

		bool repair_;
	};

	string to_string(unsigned char const *data) {
		// FIXME: we're assuming the data is zero terminated here
		return std::string(reinterpret_cast<char const *>(data));
	}

	//--------------------------------

	class mapping_emitter : public mapping_visitor {
	public:
		mapping_emitter(emitter::ptr e)
			: e_(e) {
		}

		void visit(block_address cblock, mapping const &m) {
			if (m.flags_ & M_VALID)
				e_->mapping(cblock, m.oblock_, m.flags_ & M_DIRTY);
		}

	private:
		emitter::ptr e_;
	};

	struct ignore_mapping_damage : public mapping_array_damage::damage_visitor {
		virtual void visit(missing_mappings const &d) {
		}

		virtual void visit(invalid_mapping const &d) {
		}
	};

	class fatal_mapping_damage : public mapping_array_damage::damage_visitor {
	public:
		virtual void visit(missing_mappings const &d) {
			raise();
		}

		virtual void visit(invalid_mapping const &d) {
			raise();
		}

	private:
		static void raise() {
			throw std::runtime_error("metadata contains errors (run cache_check for details).\n"
						 "perhaps you wanted to run with --repair");
		}
	};

	//--------------------------------

	string const STDOUT_PATH("-");

	bool want_stdout(string const &output) {
		return output == STDOUT_PATH;
	}

	int dump_(string const &dev, ostream &out, flags const &fs) {
		block_manager<>::ptr bm = open_bm(dev, block_io<>::READ_ONLY);
		metadata::ptr md(new metadata(bm, metadata::OPEN));
		emitter::ptr e = create_xml_emitter(out);

		superblock const &sb = md->sb_;
		e->begin_superblock(to_string(sb.uuid), sb.data_block_size,
				    sb.cache_blocks, to_string(sb.policy_name),
				    sb.policy_hint_size);

		e->begin_mappings();
		{
			mapping_emitter me(e);
			ignore_mapping_damage ignore;
			fatal_mapping_damage fatal;
			mapping_array_damage::damage_visitor &dv = fs.repair_ ?
				static_cast<mapping_array_damage::damage_visitor &>(ignore) :
				static_cast<mapping_array_damage::damage_visitor &>(fatal);
			walk_mapping_array(*md->mappings_, me, dv);
		}
		e->end_mappings();

		// walk hints


		// walk discards

		e->end_superblock();

		return 0;
	}

	int dump(string const &dev, string const &output, flags const &fs) {
		try {
			if (want_stdout(output))
				return dump_(dev, cout, fs);
			else {
				ofstream out(output.c_str());
				return dump_(dev, out, fs);
			}

		} catch (std::exception &e) {
			cerr << e.what() << endl;
			return 1;
		}
	}

	void usage(ostream &out, string const &cmd) {
		out << "Usage: " << cmd << " [options] {device|file}" << endl
		    << "Options:" << endl
		    << "  {-h|--help}" << endl
		    << "  {-o <xml file>}" << endl
		    << "  {-V|--version}" << endl
		    << "  {--repair}" << endl;
	}
}

//----------------------------------------------------------------

int main(int argc, char **argv)
{
	int c;
	flags fs;
	string output("-");
	char const shortopts[] = "ho:V";

	option const longopts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "output", required_argument, NULL, 'o' },
		{ "version", no_argument, NULL, 'V' },
		{ "repair", no_argument, NULL, 1 },
		{ NULL, no_argument, NULL, 0 }
	};

	while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
		switch(c) {
		case 1:
			fs.repair_ = true;
			break;

		case 'h':
			usage(cout, basename(argv[0]));
			return 0;

		case 'o':
			output = optarg;
			break;

		case 'V':
			cout << THIN_PROVISIONING_TOOLS_VERSION << endl;
			return 0;

		default:
			usage(cerr, basename(argv[0]));
			return 1;
		}
	}

	if (argc == optind) {
		cerr << "No input file provided." << endl;
		usage(cerr, basename(argv[0]));
		return 1;
	}

	return dump(argv[optind], output, fs);
}

//----------------------------------------------------------------
