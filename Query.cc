/* Copyright (C) 2006 - 2009 Sun Microsystems
 All rights reserved. Use is subject to license terms.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
*/

#include "mod_ndb.h"
#include "ndb_api_compat.h"
#include "query_source.h"

/* There are many varieties of query: 
   read, insert, update, and delete (e.g. HTTP GET, POST, and DELETE);
   single-row lookups and multi-row scans;
   access via primary key, unique hash index, ordered index, or scan filter;
   queries that use blobs.
   Each variant possibility is represented as a module in this file, 
   and those modules are functions that conform to the "PlanMethod" typedef.
   TO DO: PlanMethods return 0 on success or an HTTP response code on error.   
*/
typedef int PlanMethod(request_rec *, config::dir *, struct QueryItems *);

/* The modules: */
namespace Plan {
  PlanMethod SetupRead; PlanMethod SetupWrite; PlanMethod SetupDelete; // setups
  PlanMethod SetupInsert;
  PlanMethod Read;      PlanMethod Write;      PlanMethod Delete;     // actions
};  


/* A "runtime column" is the runtime complement to the config::key_col structure
*/
class runtime_col {
  public:
    char *value;
};

class index_object;   // forward declaration

/* The main Query() function has a single instance of the QueryItems structure,
   which is used to pass essential data among the modules  
*/
struct QueryItems {
  ndb_instance *i;
  const NdbDictionary::Table *tab;
  const NdbDictionary::Index *idx;
  runtime_col *keys;
  short active_index;
  index_object *idxobj;
  short key_columns_used;
  int n_filters;
  short *filter_list;
  AccessPlan plan;
  PlanMethod *op_setup;
  PlanMethod *op_action;
  mvalue *set_vals;
  data_operation *data;
  query_source *source;
};  

#include "index_object.h"


/* Utility function declarations
*/
int set_up_write(request_rec *, config::dir *, struct QueryItems *, bool);
short key_col_bin_search(char *, config::dir *);


/* Some very simple modules are fully defined here:
*/
int Plan::SetupRead(request_rec *r, config::dir *dir, struct QueryItems *q) {
  log_debug(r->server,"setup: This is a read.");
  config::index *index = 0;
  switch(q->plan) {
    case Scan:
      index = dir->index_scan;
      if(!index->name) { 
        log_debug(r->server, "Using table scan");
        /* To do: it's probably best for performance to use SF_TupScan (the 
           default) for memory tables, but SF_DiskScan for disk tables */
        return q->data->scanop->readTuples();
      }      
      /* Fall through to OrderedIndexScan */
    case OrderedIndexScan:
      if(!index) index = & dir->indexes->item(q->active_index);
      /* I'm using an undocumented form of readTuples() here, following the 
         example from select_all.cpp, because the documented one does not work 
         as advertised with the sort-flag. 
      */
      if(index->flag.sorted) {
        log_debug(r->server,"Sorting %s",index->flag.descending ? "DESC":"ASC");
        return q->data->scanop->readTuples(NdbScanOperation::LM_CommittedRead, 
                                           0, 0, true, index->flag.descending);
      }
      return q->data->scanop->readTuples(NdbOperation::LM_CommittedRead);
    default:
      return q->data->op->readTuple(NdbOperation::LM_CommittedRead);
  }
}

int Plan::SetupInsert(request_rec *r, config::dir *dir, struct QueryItems *q) { 
  log_debug(r->server,"setup: This is an insert.");
  return set_up_write(r, dir, q, true);
}

int Plan::SetupWrite(request_rec *r, config::dir *dir, struct QueryItems *q) { 
  log_debug(r->server,"setup: This is an update.");
  return set_up_write(r, dir, q, false);
}

int Plan::SetupDelete(request_rec *r, config::dir *dir, struct QueryItems *q) { 
  log_debug(r->server,"setup: This is a delete.");
  return q->data->op->deleteTuple(); 
}


/* Inlined code to test the usability of an mvalue
*/
inline bool mval_is_usable(request_rec *r, mvalue &mval) {
  if(mval.use_value > mvalue_is_good) return 1;  
  else switch(mval.use_value) {
    case err_bad_user_value:
      return 0;
    case err_bad_column:
      log_err(r->server, "Attempt to use nonexistent column in query (%s).",
              r->unparsed_uri);
      return 0;
    case err_bad_data_type:
      log_err(r->server, "Cannot use column %s: unsupported data type (%s).",
              mval.ndb_column->getName(), r->unparsed_uri);
      return 0;
    default:
      assert(0);
  }
}



/* Inlined code (called while processing both pathinfo and request params)
   which sets the items in the Q.keys array and determines the access plan.
*/
inline void set_key(request_rec *r, short &n, char *value, config::dir *dir, 
                    struct QueryItems *q) 
{
  config::key_col &keycol = dir->key_columns->item(n);

  if(keycol.is.filter) {      
    if(! q->filter_list)  /* Initialize the filter list */
      q->filter_list = (short *) 
        ap_pcalloc(r->pool, (dir->key_columns->size() * sizeof(short)));
    /* Push this filter on to the list */
    q->filter_list[q->n_filters++] = n;
  }

  q->keys[n].value = value; 
  log_debug(r->server, "Request in: $%s=%s", keycol.name, value);
  q->key_columns_used++;
 
  if(keycol.implied_plan > q->plan) {
    q->plan = keycol.implied_plan;
    q->active_index = keycol.index_id;
  }
}

// =============================================================

/* Query():
   Process an HTTP request, then formulate and run an NDB execution plan.
*/
int Query(request_rec *r, config::dir *dir, ndb_instance *i, query_source &qsource) 
{
  const NdbDictionary::Dictionary *dict;
  data_operation local_data_op = { 0, 0, 0, 0, 0};
  struct QueryItems Q = 
    { i, 0, 0,            // ndb_instance, tab, idx
      0, -1, 0, 0,        // keys, active_index, idxobj, key_columns_used 
      0, 0,               // n_filters, filter_list,
      NoPlan,             // execution plan
      Plan::SetupRead,    // setup module
      Plan::Read,         // action module
      0,                  // set_vals
      &local_data_op,     // data
      &qsource            // source
    };
  struct QueryItems *q = &Q;
  const NdbDictionary::Column *ndb_Column;
  int response_code = 0;
  mvalue mval;
  short col;
  register const char * idxname;

  // Initialize the data dictionary 
  i->db->setDatabaseName(dir->database);
  dict = i->db->getDictionary();
  q->tab = dict->getTable(dir->table);
  if(q->tab == 0) { 
    log_err(r->server, "Cannot find table %s in database %s: %s.",
             dir->table,dir->database, dict->getNdbError().message);
    i->stats.errors++;
    return ndb_handle_error(r, 500, & dict->getNdbError(), "Configuration error.");
  }  
  
  /* Initialize q->keys, the runtime array of key columns which is used
     in parallel with the configure-time array dir->key_columns.   */
  q->keys = (runtime_col *) ap_pcalloc(r->pool, 
           (dir->key_columns->size() * sizeof(runtime_col)));

    
  /* Many elements of the query plan depend on the HTTP operation --
     GET, POST, or DELETE.  Set these up here.
  */
  switch(qsource.req_method) {
    case M_GET:
      /* Write requests use the local data_operation structure (which is 
         discarded after the request), but read requests use one that is 
         stored in the ndb_instance, so that we can be access it after the
         batch of transactions is executed and fetch the results.
      */
      if(i->n_read_ops < i->server_config->max_read_operations) {
        q->data = i->data + i->n_read_ops++;
        if(dir->flag.use_etags) i->flag.use_etag = 1;
        q->data->fmt = dir->fmt;
        q->data->flag.select_star = dir->flag.select_star;
        if(dir->flag.select_star)
          q->data->n_result_cols = q->tab->getNoOfColumns();
        else {
          q->data->n_result_cols = dir->visible->size();
          q->data->aliases = dir->aliases->items();
        }
      }
      else {  /* too many read ops.  
        This error can only be fixed be reconfiguring & restarting Apache. */
        const char *msg = "Too many read operations in one transaction.";
        log_err(r->server,msg);        
        return ndb_handle_error(r, 500, NULL, msg);
      }
      
      ap_discard_request_body(r);
      // Allocate an array of result objects for all desired columns.
      // Like anything that will be stored in the ndb_instance, allocate
      // from r->connection->pool, not r->pool
      q->data->result_cols =  (MySQL::result**)
        ap_pcalloc(r->connection->pool, 
                   q->data->n_result_cols * sizeof(MySQL::result *));
      break;
    case M_POST:
      Q.op_setup = Plan::SetupWrite;
      Q.op_action = Plan::Write;
      Q.set_vals = (mvalue *) ap_pcalloc(r->pool, dir->updatable->size() * sizeof (mvalue));
      response_code = qsource.get_form_data();
      if(response_code != OK) 
        return ndb_handle_error(r, response_code, NULL, NULL);
      /* A POST with no keys is an insert, and  
         an insert has a primary key plan: */
      if(! (r->args || dir->pathinfo_size)) {
        Q.plan = PrimaryKey;
        Q.op_setup = Plan::SetupInsert;
      }
      break;
    case M_DELETE:
      if(! dir->flag.allow_delete) 
        return ndb_handle_error(r, 405, NULL, allowed_methods(r, dir));
      Q.op_setup = Plan::SetupDelete;
      Q.op_action = Plan::Delete;
      break;
    default:
      return ndb_handle_error(r, 405, NULL, allowed_methods(r, dir));
  }


  if(dir->flag.table_scan) Q.plan = Scan;

  /* A default key might have been picked during configuration.
  */
  if(dir->default_key >= 0) {
    q->active_index = dir->default_key;
    NSQL::Expr *e = dir->indexes->item(Q.active_index).constants;
    if(e) q->plan = e->implied_plan;
  }

  /* ===============================================================
     Process arguments, and then pathinfo, to determine an access plan.
     The detailed work is done within the inlined function set_key().
  */
  if(r->args) {  /* Arguments */
    register const char *c = r->args;
    char *key, *val;
    short n;
    
    while(*c && (val = ap_getword(r->pool, &c, '&'))) {
      key = ap_getword(r->pool, (const char **) &val, '=');
      ap_unescape_url(key);
      ap_unescape_url(val);
      n = key_col_bin_search(key, dir);
      if(n >= 0) 
        set_key(r, n, val, dir, &Q);
      else
        log_debug(r->server,"Unidentified key %s",key);
    }
  }   
  
  /* Pathinfo.  If args were insufficient to define a query plan (or pathinfo
     has the "always" flag), process r->path_info from right to left, 
  */
  if(dir->pathinfo_size && 
     ((Q.plan == NoPlan) || dir->flag.pathinfo_always)) {
    size_t item_len = 0;
    short element = dir->pathinfo_size - 1;
    register const char *s;
    // Set s to the end of the string, then work backwards.
    for(s = r->path_info ; *s; ++s);
    if(* (s-1) == '/') s -=2;   /* ignore a trailing slash */
    for(; s >= r->path_info && element >= 0; --s) {
      if(*s == '/') {
        set_key(r, dir->pathinfo[element--], 
                ap_pstrndup(r->pool, s+1, item_len), 
                dir, &Q);
        item_len = 0;
      }
      else item_len++;
    }
  }
  /* ===============================================================*/

  /* At this point, a GET query must have some kind of plan
  */
  if(r->method_number == M_GET && 
     ! ( dir->flag.table_scan || Q.active_index >= 0)) {
    log_debug(r->server,"No plan found for request %s", r->unparsed_uri);
    response_code = 404;
    goto abort2;
  }
  
  /* Open a transaction, if one is not already open.
     This creates an obligation to close it later, using tx->close().
  */    
  if(i->tx == 0) {
    if(i->flag.aborted) {
      log_err(r->server,"Transaction already aborted.");
      response_code = 500;
      goto abort2;
    }
    /* If this is a Primary Key query, and the primary key is the distribution
       key, then you could supply a hint here in an Ndb::Key_part_ptr.
    */
    if(!(i->tx = i->db->startTransaction())) { 
      log_err(r->server,"db->startTransaction failed: %s",
                i->db->getNdbError().message);
      response_code = 500;
      goto abort1;
    }
  }

  
  /* Now set the Query Items that depend on the access plan and index type.
     Case 1: Table Scan  */    
  if(Q.plan == Scan) {
    if(dir->index_scan->name) {
      /* "Table scan" using an ordered index: */
      idxname = dir->index_scan->name;
      if((q->idx = dict->getIndex(idxname, dir->table)) == 0) 
        goto bad_index;
      if(q->idx->getType() != NdbDictionary::Index::OrderedIndex) {
        log_err(r->server,"Configuration error: index %s:%s is not an "
                "ordered index.", dir->table, idxname);
        goto abort1;
      }
      q->idxobj = new Ordered_index_object(q, r);
    }
    else q->idxobj = new Table_Scan_object(q,r);  /* true table scan. */
  }
  else { /* Not a scan: */
    /* Case 2: Primary Key lookup (or insert) */
    if(Q.plan == PrimaryKey)
      q->idxobj = new PK_index_object(q,r);
    /* If not a PK lookup, there must be a driving index. */
    else if(Q.active_index < 0) {
      response_code = 500;
      goto abort1;
    }
    else {
      /* Not PK. Look up the active index in the data dictionary to set q->idx 
      */
      idxname = dir->indexes->item(Q.active_index).name;
      if((q->idx = dict->getIndex(idxname, dir->table)) == 0) 
        goto bad_index;
      if(Q.plan == UniqueIndexAccess) {   // Case 3: Unique Index
        if(q->idx->getType() != NdbDictionary::Index::UniqueHashIndex) {
          log_err(r->server,"Configuration error: index %s:%s is not a "
                  "unique hash index.", dir->table, idxname);
          goto abort1;          
        }
        q->idxobj = new Unique_index_object(q,r);
      }
      else if (Q.plan == OrderedIndexScan) {  // Case 4: Ordered Index
        if(q->idx->getType() != NdbDictionary::Index::OrderedIndex) {
          log_err(r->server,"Configuration error: index %s:%s is not an "
                  "ordered index.", dir->table, idxname);
          goto abort1;
        }        
        q->idxobj = new Ordered_index_object(q,r);
      }
    }
  }
  
  // Get an NdbOperation (or NdbIndexOperation, etc.)
  q->data->op = q->idxobj->get_ndb_operation(i->tx);

  // Query setup, e.g. Plan::SetupRead calls op->readTuple() 
  if(Q.op_setup(r, dir, & Q)) { // returns 0 on success
    log_debug(r->server,"Returning 404 because Q.op_setup() failed: %s",
              q->data->scanop->getNdbError().message);
    response_code = 404;
    goto abort1;
  }

  // Traverse the index parts and build the query
  if(Q.plan != Scan && Q.active_index >= 0) {
    col = dir->indexes->item(Q.active_index).first_col;

    while (col >= 0 && Q.key_columns_used-- > 0) {
      config::key_col &keycol = dir->key_columns->item(col);
      ndb_Column = q->idxobj->get_column(keycol);    

      log_debug(r->server," ** Request column_alias: %s [%s] -- value: %s", 
                 keycol.name, ndb_Column->getName(), Q.keys[col].value);
      
      MySQL::value(mval, r->pool, ndb_Column, Q.keys[col].value);
      if( (! mval_is_usable(r, mval))  ||
          (q->idxobj->set_key_part(keycol.rel_op, mval))) 
      {
          log_debug(r->server," set key failed for column %s", ndb_Column->getName())
          response_code = ndb_handle_error(r, 500, & q->data->op->getNdbError(), 
                                           "Configuration error");;
          goto abort1;
      }
      col = dir->key_columns->item(col).next_in_key;
      if(! q->idxobj->next_key_part()) break;
    }
    
    /* Constants */
    NSQL::Expr *constant = dir->indexes->item(Q.active_index).constants ;
    while(constant) {
      ndb_Column = q->idxobj->get_column(*constant);    
      MySQL::value(mval, r->pool, ndb_Column, constant->value);
      if( (!mval_is_usable(r, mval)) ||
          (q->idxobj->set_key_part(constant->rel_op, mval)))
      {
          log_err(r->server, "Failed setting column to constant %s",
                  constant->value);
          response_code = ndb_handle_error(r, 500, & q->data->op->getNdbError(),
                                           "Configuration error.");
          goto abort1;
      }
      constant = constant->next;
    }
  } /* if (plan != Scan) */
  
  // Set filters
  if(Q.plan >= Scan && Q.n_filters) {
    NdbScanFilter filter(q->data->scanop);
    NdbScanFilter::BinaryCondition cond;
    filter.begin(NdbScanFilter::AND);
    
    for(int nfilt = 0 ; nfilt < Q.n_filters ; nfilt++) {
      int n = Q.filter_list[nfilt];  
      config::key_col &keycol = dir->key_columns->item(n);
      runtime_col *filter_col = & Q.keys[n];
      ndb_Column = q->tab->getColumn(keycol.base_col_name);
      int col_id = ndb_Column->getColumnNo();
      cond = (NdbScanFilter::BinaryCondition) keycol.rel_op;

      log_debug(r->server," ** Filter %s using %s (%s)", 
                keycol.base_col_name, keycol.name, filter_col->value);

      if(cond >= NdbScanFilter::COND_LIKE) {  /* LIKE or NOT LIKE */
        /* LIKE filters also return nulls, which is not the desired result */
        if(cond == NdbScanFilter::COND_LIKE && ndb_Column->getNullable())
          filter.isnotnull(col_id);
        filter.cmp(cond, col_id, filter_col->value, strlen(filter_col->value));
      }
      else {        
        MySQL::value(mval, r->pool, ndb_Column, filter_col->value);
        if(mval.use_value == use_char)                     
          filter.cmp(cond, col_id, mval.u.val_char, mval.col_len);
        else 
          filter.cmp(cond, col_id, (&mval.u.val_char) ); 
      }
    } /*for*/                  
    filter.end();
  }
  
  // Check whether this is a JSONRequest
  if(qsource.content_type && 
     ! strcasecmp(qsource.content_type, "application/jsonrequest")) {
      log_debug(r->server, "This is a JSONRequest.");
      i->flag.jsonrequest = 1;
  }

  // Perform the action; i.e. get the value of each column
  response_code = Q.op_action(r, dir, &Q);

  // Clean up parts of Q that need to be freed
  delete q->idxobj;
  q->idxobj = 0;
  
  if(response_code == 0) {  
    if(qsource.keep_tx_open) 
      return OK;
    else
      return ExecuteAll(r, i);
  }

  abort1:
  i->tx->close();

  abort2:
  // Look at this later.  A failure of any operation causes the whole transaction
  // to be aborted?  
  log_debug(r->server,"Aborting open transaction at '%s'",r->unparsed_uri);
  if(! response_code) response_code = 500;
  i->tx = 0;  // every other op in the tx will see this and fail
  if(qsource.keep_tx_open)
    i->flag.aborted = 1;  // this will only trigger the msg on line 356.  what is the point?
  else
    i->cleanup();

  // Clean up parts of Q that need to be freed
  if(q->idxobj) delete q->idxobj;
  if(q->data->result_cols) delete[] q->data->result_cols;
  
  return response_code;
  
  bad_index:
  log_err(r->server, "mod_ndb: index %s does not exist (db: %s, table: %s)",
        idxname, dir->database, dir->table);
  response_code = 500;
  goto abort1;
}


int Plan::Read(request_rec *r, config::dir *dir, struct QueryItems *q) {  
  char **column_list = dir->visible->items();;
  const NdbDictionary::Column *col;
  unsigned int n = 0;
  const int select_star = dir->flag.select_star;

  // Set up the result columns
  for( ; n < q->data->n_result_cols ; n++) {
    col = select_star ? 
      q->tab->getColumn(n) : q->tab->getColumn(column_list[n]);
      q->data->result_cols[n] = new MySQL::result(q->data->op, col);
  }
  return 0;
}


int set_up_write(request_rec *r, config::dir *dir, 
                 struct QueryItems *q, bool is_insert) 
{ 
  const NdbDictionary::Column *col;
  bool is_interpreted = 0;
  char **column_list = dir->updatable->items();
  const char *key = 0, *val = 0;
  len_string *binary_val;

  // Iterate over the updatable columns and set up mvalues for them
  for(int n = 0; n < dir->updatable->size() ; n++) {
    key = column_list[n];
    binary_val = q->source->get_item(key);
    if(binary_val) {   
      val = binary_val->string;
      col = q->tab->getColumn(key);
      if(col) {
        mvalue &mval = q->set_vals[n];
        MySQL::value(mval, r->pool, col, val);
        if(mval.use_value == must_use_binary) {
          /* Try again with a binary value */
          MySQL::binary_value(mval, r->pool, col, binary_val);
          log_debug(r->server,"Binary update to column %s", key);
        }

        if(mval.use_value == use_interpreted) {
          is_interpreted = 1;
          log_debug(r->server,"Interpreted update; column %s = [%s]", key,val);
        }
        else log_debug(r->server,"Updating column %s = %s", key,val);
      } // end if(col)
      else log_err(r->server,"AllowUpdate list includes invalid column name %s", key);
    } // end if(binary_val)
  } // end for()
  
  // Call the aproporiate setup on the NdbOperation
  if(is_insert) 
    return q->data->op->insertTuple();
  if(is_interpreted) 
    return q->data->op->interpretedUpdateTuple();
  return q->data->op->writeTuple();
}


int Plan::Write(request_rec *r, config::dir *dir, struct QueryItems *q) {
  const NdbDictionary::Column *col;
  int eqr = 1;  
  
  // iterate over the mvalues that were set up in Plan::SetupWrite
  for(int n = 0; n < dir->updatable->size() ; n++) {
    mvalue &mval = q->set_vals[n];
    col = mval.ndb_column;
    if(col) {
      Uint64 next_value;
      if(mval_is_usable(r, mval)) {
        switch(mval.use_value) {
          case use_char:
            eqr = q->data->op->setValue(col->getColumnNo(), mval.u.val_const_char );
            break;
          case use_autoinc:
            /* to do: tunable prefetch */
            eqr = get_auto_inc_value(q->i->db, q->tab, next_value, 10);
            if(!eqr) 
              eqr = (mval.len == 8 ?
                 q->data->op->setValue(col->getColumnNo(), next_value) :
                 q->data->op->setValue(col->getColumnNo(), (Uint32) next_value));
            /* to do: else make some note of error */
            break;
          case use_null:
            eqr = q->data->op->setValue(col->getColumnNo(), (char *) NULL);
            break;
          case use_interpreted: 
            if(mval.interpreted == is_increment) 
              eqr = q->data->op->incValue(col->getColumnNo(), (Uint32) 1);
            else if(mval.interpreted == is_decrement) 
              eqr = q->data->op->subValue(col->getColumnNo(), (Uint32) 1);
            else assert(0);
            break;
          case use_blob:
            mval.u.blob_handle = q->data->op->getBlobHandle(col->getName());
            if(mval.u.blob_handle == 0)
              log_err(r->server,"Failed getting BlobHandle to set %s", 
                      col->getName());
            eqr = mval.u.blob_handle->setValue(mval.binary_info->string, 
                                               mval.binary_info->len);
            break;
          default:
            eqr = q->data->op->setValue(col->getColumnNo(), 
                                        (const char *) (&mval.u.val_char));
        }
      } /* if(mval_is_usable) */
      else eqr = -4;

      if(eqr) {
        const NdbError err = q->data->op->getNdbError();
         switch (err.code) {
          case 4217:
          case 4218:
            log_err(r->server,"Schema error. [%d], %s", err.code, err.message);
            return ndb_handle_error(r, 500, & err, 0);
          default:
            log_debug(r->server,"setValue() failed: [%d] %s", err.code, err.message);
            return ndb_handle_error(r, 500, & err, 0);
        }
      }
    }
  } // for()
  return eqr;
}


int Plan::Delete(request_rec *r, config::dir *dir, struct QueryItems *q) {
  log_debug(r->server,"Deleting Row %s","")
  return 0;
}


/* Based on Kernighan's C binsearch from TPOP pg. 31
*/
short key_col_bin_search(char *name, config::dir *dir) {
  int low = 0;
  int high = dir->key_columns->size() - 1;
  int mid;
  register int cmp;
  
  while ( low <= high ) {
    mid = (low + high) / 2;
    cmp = strcmp(name, dir->key_columns->item(mid).name);
    if(cmp < 0) 
      high = mid - 1;
    else if (cmp > 0)
      low = mid + 1;
    else
      return mid;
  }
  return -1;
}

