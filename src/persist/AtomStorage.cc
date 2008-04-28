/*
 * FUNCTION:
 * Persistent Atom storage, SQL-backed.
 *
 * Atoms are saved to, and restored from, and SQL DB.
 * Atoms are identified by means of unique ID's, which are taken to
 * be the atom Handles, as maintained by the TLB. In particular, the
 * system here depends on the handles in the TLB and in the SQL DB
 * to be consistent (i.e. kept in sync).
 *
 * HISTORY:
 * Copyright (c) 2008 Linas Vepstas <linas@linas.org>
 */

#include <stdlib.h>

#include "odbcxx.h"
#include "Atom.h"
#include "ClassServer.h"
#include "Foreach.h"
#include "Link.h"
#include "Node.h"
#include "SimpleTruthValue.h"
#include "TLB.h"
#include "TruthValue.h"
#include "type_codes.h"

#include "AtomStorage.h"

using namespace opencog;

/* ================================================================ */

/**
 * Utility class, hangs on to a single response to an SQL query,
 * and provides routines to parse it, i.e. walk the rows and columns,
 * converting each row into an Atom, or Edge.
 *
 * Intended to be allocated on stack, to avoid malloc overhead.
 * Methods are intended to be inlined, so as to avoid subroutine 
 * call overhead.  It really *is* supposed to be a convenience wrapper. :-)
 */
class AtomStorage::Response
{
	public:
		ODBCRecordSet *rs;

		// Temporary cache of info about atom being assembled.
		Handle handle;
		Type itype;
		const char * name;
		double mean;
		double count;

		bool create_atom_column_cb(const char *colname, const char * colvalue)
		{
			printf ("%s = %s\n", colname, colvalue);
			if (!strcmp(colname, "type"))
			{
				itype = atoi(colvalue);
			}
			else if (!strcmp(colname, "name"))
			{
				name = colvalue;
			}
			else if (!strcmp(colname, "stv_mean"))
			{
				mean = atof(colvalue);
			}
			else if (!strcmp(colname, "stv_count"))
			{
				count = atof(colvalue);
			}
			else if (!strcmp(colname, "uuid"))
			{
				handle = strtoul(colvalue, NULL, 10);
			}
			return false;
		}
		bool create_atom_cb(void)
		{
			printf ("---- New atom found ----\n");
			rs->foreach_column(&Response::create_atom_column_cb, this);

			return false;
		}

		AtomTable *table;
		AtomStorage *store;
		bool load_all_atoms_cb(void)
		{
			printf ("---- New atom found ----\n");
			rs->foreach_column(&Response::create_atom_column_cb, this);

			Atom *atom = store->makeAtom(*this, handle);
			table->add(atom, false);

			return false;
		}

		bool row_exists;
		bool row_exists_cb(void)
		{
			row_exists = true;
			return false;
		}

		// Temporary cache of info about the outgoing set.
		std::vector<Handle> *outvec;
		Handle dst;
		int pos;

		bool create_edge_cb(void)
		{
			// printf ("---- New edge found ----\n");
			rs->foreach_column(&Response::create_edge_column_cb, this);
			int sz = outvec->size();
			if (sz <= pos) outvec->resize(pos+1);
			outvec->at(pos) = dst;
			return false;
		}
		bool create_edge_column_cb(const char *colname, const char * colvalue)
		{
			// printf ("%s = %s\n", colname, colvalue);
			if (!strcmp(colname, "dst_uuid"))
			{
				dst = strtoul(colvalue, (char **) NULL, 10);
			}
			else if (!strcmp(colname, "pos"))
			{
				pos = atoi(colvalue);
			}
			return false;
		}

#ifdef OUT_OF_LINE_TVS
		// Callbacks for SimpleTruthValues
		int tvid;
		bool create_tv_cb(void)
		{
			// printf ("---- New SimpleTV found ----\n");
			rs->foreach_column(&Response::create_tv_column_cb, this);
			return false;
		}
		bool create_tv_column_cb(const char *colname, const char * colvalue)
		{
			printf ("%s = %s\n", colname, colvalue);
			if (!strcmp(colname, "mean"))
			{
				mean = atof(colvalue);
			}
			else if (!strcmp(colname, "count"))
			{
				count = atof(colvalue);
			}
			return false;
		}

#endif /* OUT_OF_LINE_TVS */

		int intval;
		bool intval_cb(void)
		{
			rs->foreach_column(&Response::intval_column_cb, this);
			return false;
		}
		bool intval_column_cb(const char *colname, const char * colvalue)
		{
			// we're not going to bother to check the column name ... 
			intval = atoi(colvalue);
			return false;
		}

};

bool AtomStorage::idExists(const char * buff)
{
	Response rp;
	rp.row_exists = false;
	rp.rs = db_conn->exec(buff);
	rp.rs->foreach_row(&Response::row_exists_cb, &rp);
	rp.rs->release();
	return rp.row_exists;
}

/* ================================================================ */
#define BUFSZ 250

/**
 * Callback class, whose method is invoked on each outgoing edge.
 * The callback constructs an SQL query to store the edge.
 */
class AtomStorage::Outgoing
{
	private:
		ODBCConnection *db_conn;
		unsigned int pos;
		Handle src_handle;
	public:
		Outgoing (ODBCConnection *c, Handle h)
		{
			db_conn = c;
			src_handle = h;
			pos = 0;
		}
		bool each_handle (Handle h)
		{
			char buff[BUFSZ];
			snprintf(buff, BUFSZ, "INSERT  INTO Edges "
                 "(src_uuid, dst_uuid, pos) VALUES (%lu, %lu, %u);",
			        (unsigned long) src_handle, (unsigned long) h, pos);

			Response rp;
			rp.rs = db_conn->exec(buff);
			rp.rs->release();
			pos ++;
			return false;
		}
};

/* ================================================================ */

AtomStorage::AtomStorage(const char * dbname, 
                         const char * username,
                         const char * authentication)
{
	db_conn = new ODBCConnection(dbname, username, authentication);
	TLB::uuid = getMaxUUID();
}

AtomStorage::~AtomStorage()
{
	setMaxUUID(TLB::uuid);
	delete db_conn;
}

/* ================================================================ */

#define STMT(colname,val) { \
	if(update) { \
		if (notfirst) { cols += ", "; } else notfirst = 1; \
		cols += colname; \
		cols += " = "; \
		cols += val; \
	} else { \
		if (notfirst) { cols += ", "; vals += ", "; } else notfirst = 1; \
		cols += colname; \
		vals += val; \
	} \
}

#define STMTI(colname,ival) { \
	char buff[BUFSZ]; \
	snprintf(buff, BUFSZ, "%u", ival); \
	STMT(colname, buff); \
}

#define STMTF(colname,fval) { \
	char buff[BUFSZ]; \
	snprintf(buff, BUFSZ, "%12.8g", fval); \
	STMT(colname, buff); \
}

/**
 * Store the outgoing set of the atom.
 * Handle h must be the handle for the atom; its passed as an arg to 
 * avoid having to look it up.
 */
void AtomStorage::storeOutgoing(Atom *atom, Handle h)
{
	Outgoing out(db_conn, h);

	foreach_outgoing_handle(h, &Outgoing::each_handle, &out);
}

/* ================================================================ */

#ifdef OUT_OF_LINE_TVS
/**
 * Return true if the indicated handle exists in the storage.
 */
bool AtomStorage::tvExists(int tvid)
{
	char buff[BUFSZ];
	snprintf(buff, BUFSZ, "SELECT tvid FROM SimpleTVs WHERE tvid = %u;", tvid);
	return idExists(buff);
}

/**
 * Store the truthvalue of the atom.
 * Handle h must be the handle for the atom; its passed as an arg to 
 * avoid having to look it up.
 */
int AtomStorage::storeTruthValue(Atom *atom, Handle h)
{
	int notfirst = 0;
	std::string cols;
	std::string vals;
	std::string coda;

	const TruthValue &tv = atom->getTruthValue();

	const SimpleTruthValue *stv = dynamic_cast<const SimpleTruthValue *>(&tv);
	if (NULL == stv)
	{
		fprintf(stderr, "Error: non-simple truth values are not handled\n");
		return 0;
	}

	int tvid = TVID(tv);

	// If its a stock truth value, there is nothing to do.
	if (tvid <= 4) return tvid;

	// Use the TLB Handle as the UUID.
	char tvidbuff[BUFSZ];
	snprintf(tvidbuff, BUFSZ, "%u", tvid);

	bool update = tvExists(tvid);
	if (update)
	{
		cols = "UPDATE SimpleTVs SET ";
		vals = "";
		coda = " WHERE tvid = ";
		coda += tvidbuff;
		coda += ";";
	}
	else
	{
		cols = "INSERT INTO SimpleTVs (";
		vals = ") VALUES (";
		coda = ");";
		STMT("tvid", tvidbuff);
	}

	STMTF("mean", tv.getMean());
	STMTF("count", tv.getCount());

	std::string qry = cols + vals + coda;
	Response rp;
	rp.rs = db_conn->exec(qry.c_str());
	rp.rs->release();

	return tvid;
}

/**
 * Return a new, unique ID for every truth value
 */
int AtomStorage::TVID(const TruthValue &tv)
{
	if (tv == TruthValue::NULL_TV()) return 0;
	if (tv == TruthValue::DEFAULT_TV()) return 1;
	if (tv == TruthValue::FALSE_TV()) return 2;
	if (tv == TruthValue::TRUE_TV()) return 3;
	if (tv == TruthValue::TRIVIAL_TV()) return 4;

	Response rp;
	rp.rs = db_conn->exec("SELECT NEXTVAL('tvid_seq');");
	rp.rs->foreach_row(&Response::tvid_seq_cb, &rp);
	return rp.tvid;
}

TruthValue* AtomStorage::getTV(int tvid)
{
	if (0 == tvid) return (TruthValue *) & TruthValue::NULL_TV();
	if (1 == tvid) return (TruthValue *) & TruthValue::DEFAULT_TV();
	if (2 == tvid) return (TruthValue *) & TruthValue::FALSE_TV();
	if (3 == tvid) return (TruthValue *) & TruthValue::TRUE_TV();
	if (4 == tvid) return (TruthValue *) & TruthValue::TRIVIAL_TV();

	char buff[BUFSZ];
	snprintf(buff, BUFSZ, "SELECT * FROM SimpleTVs WHERE tvid = %u;", tvid);

	Response rp;
	rp.rs = db_conn->exec(buff);
	rp.rs->foreach_row(&Response::create_tv_cb, &rp);
	rp.rs->release();

	SimpleTruthValue *stv = new SimpleTruthValue(rp.mean,rp.count);
	return stv;
}

#endif /* OUT_OF_LINE_TVS */

/* ================================================================ */

/**
 * Store the indicated atom.
 * Store its truth values too.
 */
void AtomStorage::storeAtom(Atom *atom)
{
	int notfirst = 0;
	std::string cols;
	std::string vals;
	std::string coda;

	Handle h = TLB::getHandle(atom);

	// Use the TLB Handle as the UUID.
	char uuidbuff[BUFSZ];
	snprintf(uuidbuff, BUFSZ, "%lu", (unsigned long) h);

	bool update = atomExists(h);
	if (update)
	{
		cols = "UPDATE Atoms SET ";
		vals = "";
		coda = " WHERE uuid = ";
		coda += uuidbuff;
		coda += ";";
	}
	else
	{
		cols = "INSERT INTO Atoms (";
		vals = ") VALUES (";
		coda = ");";

		STMT("uuid", uuidbuff);
	}

	// Store the atom UUID
	Type t = atom->getType();
	STMTI("type", t);

	// Store the node name, if its a node
	Node *n = dynamic_cast<Node *>(atom);
	if (n)
	{
		std::string qname = "'";
		qname += n->getName();
		qname += "'";
		STMT("name", qname);
	}

	// Store the truth value
	const TruthValue &tv = atom->getTruthValue();
	const SimpleTruthValue *stv = dynamic_cast<const SimpleTruthValue *>(&tv);
	if (NULL == stv)
	{
		fprintf(stderr, "Error: non-simple truth values are not handled\n");
		return;
	}
	STMTF("stv_mean", tv.getMean());
	STMTF("stv_count", tv.getCount());

	std::string qry = cols + vals + coda;
	Response rp;
	rp.rs = db_conn->exec(qry.c_str());
	rp.rs->release();

	// Store the outgoing handles only if we are storing for the first
	// time, otherwise do nothing. The semantics is that, once the 
	// outgoing set has been determined, it cannot be changed.
	if (false == update)
	{
		storeOutgoing(atom, h);
	}
}

/* ================================================================ */
/**
 * Return true if the indicated handle exists in the storage.
 */
bool AtomStorage::atomExists(Handle h)
{
	char buff[BUFSZ];
	snprintf(buff, BUFSZ, "SELECT uuid FROM Atoms WHERE uuid = %lu;", (unsigned long) h);
	return idExists(buff);
}

/* ================================================================ */

void AtomStorage::getOutgoing(std::vector<Handle> &outv, Handle h)
{
	char buff[BUFSZ];
	snprintf(buff, BUFSZ, "SELECT * FROM Edges WHERE src_uuid = %lu;", (unsigned long) h);

	Response rp;
	rp.rs = db_conn->exec(buff);
	rp.outvec = &outv;
	rp.rs->foreach_row(&Response::create_edge_cb, &rp);
	rp.rs->release();
}

/* ================================================================ */
/**
 * Create a new atom, retreived from storage
 *
 * This method does *not* register the atom with any atomtable/atomspace
 * However, it does register with the TLB, as the SQL uuids and the 
 * TLB Handles must be kept in sync, or all hell breaks loose.
 */
Atom * AtomStorage::getAtom(Handle h)
{
	char buff[BUFSZ];
	snprintf(buff, BUFSZ, "SELECT * FROM Atoms WHERE uuid = %lu;", (unsigned long) h);

	Response rp;
	rp.rs = db_conn->exec(buff);
	rp.rs->foreach_row(&Response::create_atom_cb, &rp);

	Atom *atom = makeAtom(rp, h);

	rp.rs->release();

	return atom;
}

Atom * AtomStorage::makeAtom(Response &rp, Handle h)
{
	// Now that we know everything about an atom, actually construct one.
	Atom *atom = TLB::getAtom(h);

	if (NULL == atom)
	{
		if (ClassServer::isAssignableFrom(NODE, rp.itype))
		{
			atom = new Node(rp.itype, rp.name);
		}
		else
		{
			std::vector<Handle> outvec;
			getOutgoing(outvec, h);
			atom = new Link(rp.itype, outvec);
		}

		// Make sure that the handle in the TLB is synced with 
		// the handle we use in the database.
		TLB::addAtom(atom, h);
	}
	else
	{
		// Perform at least some basic sanity checking ...
		if (rp.itype != atom->getType())
		{
			fprintf(stderr,
				"Error: mismatched atom type for existing atom! uuid=%lu\n",
				(unsigned long) h);
		}
	}

	// Now get the truth value
	SimpleTruthValue *stv = new SimpleTruthValue(rp.mean, rp.count);
	atom->setTruthValue(*stv);

	return atom;
}

/* ================================================================ */

void AtomStorage::load(AtomTable *table)
{
	Response rp;
	rp.table = table;
	rp.store = this;

	rp.rs = db_conn->exec("SELECT * FROM Atoms;");
	rp.rs->foreach_row(&Response::load_all_atoms_cb, &rp);

	rp.rs->release();

	table->scrubIncoming();
}

void AtomStorage::store(AtomTable *table)
{
}

/* ================================================================ */

unsigned long AtomStorage::getMaxUUID()
{
	Response rp;
	rp.rs = db_conn->exec("SELECT max_uuid FROM Global;");
	rp.rs->foreach_row(&Response::intval_cb, &rp);
	rp.rs->release();
	return rp.intval;
}

void AtomStorage::setMaxUUID(unsigned long uuid)
{
	char buff[BUFSZ];
	snprintf(buff, BUFSZ, "UPDATE Global SET max_uuid = %lu;", (unsigned long) uuid);

	Response rp;
	rp.rs = db_conn->exec(buff);
	rp.rs->release();
}

/* ============================= END OF FILE ================= */
