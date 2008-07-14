#include "ruby.h"

#include "c-client-src/zookeeper.h"
#include <errno.h>

#include <stdio.h>

static VALUE Zookeeper = Qnil;
static VALUE eNoNode = Qnil;
static VALUE eBadVersion = Qnil;

struct zk_rb_data {
  zhandle_t *zh;
  clientid_t myid;
};

/*static void watcher(zhandle_t *zzh, int type, int state, const char *path) {
  fprintf(stderr,"Watcher %d state = %d for %s\n", type, state, (path ? path: "null"));
  }*/

static void check_errors(int rc) {
  switch (rc) {
  case ZOK: /* all good! */ break;
  case ZBADARGUMENTS: rb_raise(rb_eRuntimeError, "invalid input parameters");
  case ZMARSHALLINGERROR: rb_raise(rb_eRuntimeError, "failed to marshall a request; possibly out of memory");
  case ZOPERATIONTIMEOUT: rb_raise(rb_eRuntimeError, "failed to flush the buffers within the specified timeout");
  case ZCONNECTIONLOSS: rb_raise(rb_eRuntimeError, "a network error occured while attempting to send request to server");
  case ZSYSTEMERROR: rb_raise(rb_eRuntimeError, "a system (OS) error occured; it's worth checking errno to get details");
  case ZNONODE: rb_raise(eNoNode, "the node does not exist");
  case ZNOAUTH: rb_raise(rb_eRuntimeError, "the client does not have permission");
  case ZBADVERSION: rb_raise(eBadVersion, "expected version does not match actual version");
  case ZINVALIDSTATE: rb_raise(rb_eRuntimeError, "zhandle state is either SESSION_EXPIRED_STATE or AUTH_FAILED_STATE");
  case ZNODEEXISTS: rb_raise(rb_eRuntimeError, "the node already exists");
  case ZNOCHILDRENFOREPHEMERALS: rb_raise(rb_eRuntimeError, "cannot create children of ephemeral nodes");
  case ZINVALIDACL: rb_raise(rb_eRuntimeError, "invalid ACL specified");
  default: rb_raise(rb_eRuntimeError, "unknown error returned from zookeeper: %d", rc);
  }
}

static void free_zk_rb_data(struct zk_rb_data* ptr) {
  /*fprintf(stderr, "free zk_rb_data at %p\n", ptr);*/
  zookeeper_close(ptr->zh);
}

static VALUE method_initialize(VALUE self, VALUE hostPort) {
  VALUE data;
  struct zk_rb_data* zk = NULL;

  Check_Type(hostPort, T_STRING);

  data = Data_Make_Struct(Zookeeper, struct zk_rb_data, 0, free_zk_rb_data, zk);

  zoo_set_debug_level(LOG_LEVEL_DEBUG);
  zoo_deterministic_conn_order(1); // enable deterministic order
  zk->zh = zookeeper_init(RSTRING(hostPort)->ptr, NULL, 10000, &zk->myid, 0, 0);
  if (!zk->zh) {
    rb_raise(rb_eRuntimeError, "error connecting to zookeeper: %d", errno);
  }

  rb_iv_set(self, "@data", data);

  return Qnil;
}

static VALUE method_ls(VALUE self, VALUE path) {
  struct zk_rb_data* zk;
  struct String_vector strings;
  int i;
  VALUE output;

  Check_Type(path, T_STRING);
  Data_Get_Struct(rb_iv_get(self, "@data"), struct zk_rb_data, zk);

  check_errors(zoo_get_children(zk->zh, RSTRING(path)->ptr, 0, &strings));

  output = rb_ary_new();
  for (i = 0; i < strings.count; ++i) {
    rb_ary_push(output, rb_str_new2(strings.data[i]));
  }
  return output;
}

static VALUE method_create(VALUE self, VALUE path, VALUE value, VALUE flags) {
  struct zk_rb_data* zk;
  char realpath[10240];

  Check_Type(path, T_STRING);
  Check_Type(value, T_STRING);
  Check_Type(flags, T_FIXNUM);
  Data_Get_Struct(rb_iv_get(self, "@data"), struct zk_rb_data, zk);

  check_errors(zoo_create(zk->zh, RSTRING(path)->ptr, RSTRING(value)->ptr, RSTRING(value)->len,
			  &OPEN_ACL_UNSAFE, FIX2INT(flags), realpath, 10240));

  return rb_str_new2(realpath);
}

static VALUE method_get(VALUE self, VALUE path) {
  struct zk_rb_data* zk;
  char data[1024];
  int data_len = 1024;
  struct Stat stat;
  VALUE output;

  Check_Type(path, T_STRING);
  Data_Get_Struct(rb_iv_get(self, "@data"), struct zk_rb_data, zk);
  
  check_errors(zoo_get(zk->zh, RSTRING(path)->ptr, 0, data, &data_len, &stat));
  /*printf("got some data; version=%d\n", stat.version);*/

  output = rb_ary_new();
  rb_ary_push(output, rb_str_new(data, data_len));
  rb_ary_push(output, INT2FIX(stat.version));
  return output;
}

static VALUE method_set(VALUE self, VALUE path, VALUE data, VALUE version) {
  struct zk_rb_data* zk;

  Check_Type(path, T_STRING);
  Check_Type(data, T_STRING);
  Check_Type(version, T_FIXNUM);
  Data_Get_Struct(rb_iv_get(self, "@data"), struct zk_rb_data, zk);
  
  check_errors(zoo_set(zk->zh, RSTRING(path)->ptr, RSTRING(data)->ptr, RSTRING(data)->len, FIX2INT(version)));

  return Qnil;
}

void Init_c_zookeeper() {
  Zookeeper = rb_define_class("CZookeeper", rb_cObject);
  eNoNode = rb_define_class_under(Zookeeper, "NoNodeError", rb_eRuntimeError);
  eBadVersion = rb_define_class_under(Zookeeper, "BadVersionError", rb_eRuntimeError);
  rb_define_method(Zookeeper, "initialize", method_initialize, 1);
  rb_define_method(Zookeeper, "ls", method_ls, 1);
  rb_define_method(Zookeeper, "create", method_create, 3);
  rb_define_method(Zookeeper, "get", method_get, 1);
  rb_define_method(Zookeeper, "set", method_set, 3);
}
