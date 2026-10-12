// Own isolated experiment: pinned SQLite, no host/game calls or user databases.
#include "../external/sco-core/third_party/sqlite/sqlite3.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

static void require(bool ok, const char* why) { if (!ok) { std::fprintf(stderr,"FAIL %s\n",why); std::exit(1); } }
static void exec(sqlite3* db, const char* sql) { require(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)==SQLITE_OK,sql); }
static std::string upper(std::string p) {
    while (!p.empty() && static_cast<unsigned char>(p.back())==255) p.pop_back();
    if (!p.empty()) p.back()=static_cast<char>(static_cast<unsigned char>(p.back())+1);
    return p;
}
struct Query {
    sqlite3_stmt* stmt=nullptr;
    Query(sqlite3* db, const std::string& prefix, const std::string* after, bool optimized) {
        const auto hi=upper(prefix);
        const bool strict=optimized && after && *after>=prefix;
        std::string sql="SELECT key FROM sco_kv WHERE key "; sql+=strict?"> ?1":">= ?1";
        if (!hi.empty()) sql+=" AND key < ?2";
        if (after && !optimized) sql+=" AND key > ?3";
        sql+=" ORDER BY key LIMIT 1";
        require(sqlite3_prepare_v2(db,sql.c_str(),-1,&stmt,nullptr)==SQLITE_OK,"prepare");
        const auto& low=strict?*after:prefix;
        require(sqlite3_bind_text(stmt,1,low.data(),static_cast<int>(low.size()),SQLITE_TRANSIENT)==SQLITE_OK,"bind low");
        if (!hi.empty()) require(sqlite3_bind_text(stmt,2,hi.data(),static_cast<int>(hi.size()),SQLITE_TRANSIENT)==SQLITE_OK,"bind high");
        if (after && !optimized) require(sqlite3_bind_text(stmt,3,after->data(),static_cast<int>(after->size()),SQLITE_TRANSIENT)==SQLITE_OK,"bind after");
    }
    ~Query() { sqlite3_finalize(stmt); }
    std::string read(bool& found) {
        int rc=sqlite3_step(stmt); require(rc==SQLITE_ROW || rc==SQLITE_DONE,"step"); found=rc==SQLITE_ROW;
        if (!found) return {};
        return std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt,0)),sqlite3_column_bytes(stmt,0));
    }
};
static void put(sqlite3* db,const std::string& key,const std::string& value) {
    sqlite3_stmt* q=nullptr;
    require(sqlite3_prepare_v2(db,"INSERT INTO sco_kv VALUES(?1,?2)",-1,&q,nullptr)==SQLITE_OK,"prepare put");
    sqlite3_bind_text(q,1,key.data(),static_cast<int>(key.size()),SQLITE_TRANSIENT);
    sqlite3_bind_blob(q,2,value.data(),static_cast<int>(value.size()),SQLITE_TRANSIENT);
    require(sqlite3_step(q)==SQLITE_DONE,"insert"); sqlite3_finalize(q);
}
static int scalar(sqlite3* db,const char* sql) {
    sqlite3_stmt* q=nullptr; require(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"scalar prepare");
    require(sqlite3_step(q)==SQLITE_ROW,"scalar row"); int n=sqlite3_column_int(q,0); sqlite3_finalize(q); return n;
}
int wmain(int argc,wchar_t** argv) {
    require(argc==2,"usage: storage-sql-benchmark NEW_OUTPUT_FOLDER");
    const std::filesystem::path out(argv[1]);
    require(!std::filesystem::exists(out),"output must be a new folder");
    require(std::filesystem::create_directories(out),"create isolated output");
    sqlite3* db=nullptr;
    const auto database=(out/"synthetic.sqlite3").string();
    require(sqlite3_open(database.c_str(),&db)==SQLITE_OK,"open synthetic database");
    exec(db,"CREATE TABLE sco_kv(key TEXT PRIMARY KEY,value BLOB NOT NULL) WITHOUT ROWID");
    std::vector<std::string> keys={"","a","a0","b","gen.","gen.0000","gen.9999","Рус","Русский","Русь","Я",std::string("x\xff",2)};
    std::mt19937 rng(123456);
    for (int i=0;i<1000;++i) keys.push_back("r"+std::to_string(rng()%100000));
    std::sort(keys.begin(),keys.end()); keys.erase(std::unique(keys.begin(),keys.end()),keys.end());
    exec(db,"BEGIN"); for (const auto& key:keys) put(db,key,"v"); exec(db,"COMMIT");
    std::vector<std::string> prefixes={"","a","gen.","missing","Рус","Я",std::string("x\xff",2)};
    for (int i=0;i<100;++i) prefixes.push_back("r"+std::to_string(rng()%100));
    unsigned checks=0;
    for (const auto& prefix:prefixes) for (int j=-1;j<static_cast<int>(keys.size());++j) {
        const std::string* after=j<0?nullptr:&keys[j];
        bool f1=false,f2=false;
        Query original(db,prefix,after,false), improved(db,prefix,after,true);
        const auto a=original.read(f1), b=improved.read(f2);
        require(f1==f2 && a==b,"optimized differs from original");
        const auto hi=upper(prefix);
        const auto expected=std::find_if(keys.begin(),keys.end(),[&](const auto& key) {
            return key>=prefix && (hi.empty() || key<hi) && (!after || key>*after);
        });
        require(f1==(expected!=keys.end()) && (!f1 || a==*expected),"independent range oracle"); ++checks;
    }
    exec(db,"DELETE FROM sco_kv; BEGIN");
    for (int i=0;i<3000;++i) { char key[32]; std::snprintf(key,sizeof(key),"gen.%08d",i); put(db,key,"v"); }
    exec(db,"COMMIT");
    std::ofstream times(out/"query.csv"); times<<"variant,position,iteration,ms,vm_steps,fullscan_steps\n";
    for (bool optimized:{false,true}) for (int pos:{1,300,1500,2999}) {
        char key[32]; std::snprintf(key,sizeof(key),"gen.%08d",pos); const std::string after(key);
        for (int rep=0;rep<20;++rep) {
            Query q(db,"gen.",&after,optimized); bool found;
            const auto start=std::chrono::steady_clock::now(); q.read(found);
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            times<<(optimized?"single-bound":"original")<<','<<pos<<','<<rep<<','<<ms<<','
                <<sqlite3_stmt_status(q.stmt,SQLITE_STMTSTATUS_VM_STEP,0)<<','<<sqlite3_stmt_status(q.stmt,SQLITE_STMTSTATUS_FULLSCAN_STEP,0)<<'\n';
        }
    }
    require(sqlite3_close(db)==SQLITE_OK,"close synthetic database");
    // Logical SQLite page quota only: no claim about WAL, FULL latency or disk exhaustion.
    std::ofstream quota(out/"quota.csv"); quota<<"snapshot_bytes,keep,old_count,new_commit,page_count,page_size\n";
    for (int bytes:{262144,1048576,4194304,8388608,16097280}) for (int keep:{1,3,4}) {
        require(sqlite3_open(":memory:",&db)==SQLITE_OK,"quota memory database");
        exec(db,"PRAGMA page_size=4096; PRAGMA max_page_count=16384; CREATE TABLE snapshots(id INTEGER PRIMARY KEY,payload BLOB)");
        for (int i=0;i<keep;++i) {
            sqlite3_stmt* q=nullptr; sqlite3_prepare_v2(db,"INSERT INTO snapshots VALUES(?1,zeroblob(?2))",-1,&q,nullptr);
            sqlite3_bind_int(q,1,i); sqlite3_bind_int(q,2,bytes); require(sqlite3_step(q)==SQLITE_DONE,"populate prior snapshots"); sqlite3_finalize(q);
        }
        exec(db,"BEGIN IMMEDIATE"); sqlite3_stmt* q=nullptr;
        sqlite3_prepare_v2(db,"INSERT INTO snapshots VALUES(?1,zeroblob(?2))",-1,&q,nullptr);
        sqlite3_bind_int(q,1,keep); sqlite3_bind_int(q,2,bytes); int rc=sqlite3_step(q); sqlite3_finalize(q);
        require(rc==SQLITE_DONE || rc==SQLITE_FULL,"quota insert result");
        if (rc==SQLITE_DONE) exec(db,"COMMIT"); else if (!sqlite3_get_autocommit(db)) exec(db,"ROLLBACK");
        const int count=scalar(db,"SELECT count(*) FROM snapshots");
        require(count==keep+(rc==SQLITE_DONE?1:0),"quota failure preserved old snapshots");
        quota<<bytes<<','<<keep<<','<<keep<<','<<(rc==SQLITE_DONE?"saved":"full")<<','<<scalar(db,"PRAGMA page_count")<<','<<scalar(db,"PRAGMA page_size")<<'\n';
        require(sqlite3_close(db)==SQLITE_OK,"close quota database");
    }
    std::ofstream info(out/"environment.txt"); info<<"SQLite "<<sqlite3_libversion()<<"\n"<<sqlite3_sourceid()<<"\nrange checks="<<checks<<"\n";
    std::printf("PASS %u range checks; pinned SQLite %s; query counters and quota results saved\n",checks,sqlite3_libversion());
}

