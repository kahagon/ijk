@ok = unnamed_addr constant [4 x i8] c"ok\0A\00"
@hello = constant [14 x i8] c"Hello, World\0A\00"

%string_data_t = type { i32, i8* }
%typed_value_t = type { i8, i64, double, %string_data_t* }
%stack_t = type { i64, i64, %typed_value_t** }

declare i32 @puts(i8*)


define %typed_value_t* @function_abstract() {
entry:
  %stack_p = call %stack_t* @stack_alloc(i64 1024)
  call void @stack_init(%stack_t* %stack_p)
 
  ; constcut string_data_t 
  %hello_constant_p = getelementptr [14 x i8]* @hello, i64 0, i64 0
  %hello_p = alloca %string_data_t
  %hello_data_pp = getelementptr %string_data_t* %hello_p, i64 0, i32 1
  store i8* %hello_constant_p, i8** %hello_data_pp
  %hello_size_p = getelementptr %string_data_t* %hello_p, i64 0, i32 0
  store i32 14, i32* %hello_size_p

  ; construct typed_value_t
  %value_p = alloca %typed_value_t
  call void @typed_value_set_string(%typed_value_t* %value_p, %string_data_t* %hello_p)
  call void @stack_push(%stack_t* %stack_p, %typed_value_t* %value_p)
 
  call void @hhas_print(%stack_t* %stack_p) 
  %retval_p = call %typed_value_t* @stack_pop(%stack_t* %stack_p)
  
  ret %typed_value_t* %retval_p
}

define void @hhas_print(%stack_t* %stack_p) {
  %tv_p = call %typed_value_t* @stack_pop(%stack_t* %stack_p)
  %str_data_pp = getelementptr %typed_value_t* %tv_p, i64 0, i32 3
  %str_data_p = load %string_data_t** %str_data_pp
  %str_pp = getelementptr %string_data_t* %str_data_p, i64 0, i32 1
  %str_p = load i8** %str_pp
  call i32 @puts(i8* %str_p)

  %retval_p = alloca %typed_value_t
  call void @typed_value_set_int(%typed_value_t* %retval_p, i64 1)
  call void @stack_push(%stack_t* %stack_p, %typed_value_t* %retval_p)

  ret void
}

define void @typed_value_set_int(%typed_value_t* %tv_p, i64 %num) {
  %tv_type_p = getelementptr %typed_value_t* %tv_p, i64 0, i32 0
  store i8 10, i8* %tv_type_p
  %tv_num_p = getelementptr %typed_value_t* %tv_p, i64 0, i32 1
  store i64 %num, i64* %tv_num_p
  ret void
}

define void @typed_value_set_string(%typed_value_t* %tv_p, %string_data_t* %str_p) {
  %tv_type_p = getelementptr %typed_value_t* %tv_p, i64 0, i32 0

  ; set type "string"
  store i8 19, i8* %tv_type_p

  %str_data_pp = getelementptr %typed_value_t* %tv_p, i64 0, i32 3
  store %string_data_t* %str_p, %string_data_t** %str_data_pp
  ret void
}

define %stack_t* @stack_alloc(i64 %size) {
  %stack_p = alloca %stack_t, i64 %size
  %max_size_p = getelementptr %stack_t* %stack_p, i64 0, i32 1
  store i64 %size, i64* %max_size_p

  %store_pp = alloca %typed_value_t*, i64 %size
  %store_ppp = getelementptr %stack_t* %stack_p, i64 0, i32 2
  store %typed_value_t** %store_pp, %typed_value_t*** %store_ppp

  ret %stack_t* %stack_p
}

define void @stack_init(%stack_t* %stack_p) {
  %size_p = getelementptr %stack_t* %stack_p, i64 0, i32 0
  store i64 0, i64* %size_p
  ret void
}

define %typed_value_t* @stack_pop(%stack_t* %stack_p) {
  %size_p = getelementptr %stack_t* %stack_p, i64 0, i32 0
  %size = load i64* %size_p
  %new_size = sub i64 %size, 1
  store i64 %new_size, i64* %size_p

  %store_ppp = getelementptr %stack_t* %stack_p, i64 0, i32 2
  %store_pp = load %typed_value_t*** %store_ppp
  %retval_pp = getelementptr %typed_value_t** %store_pp, i64 %size
  %retval_p = load %typed_value_t** %retval_pp

  ret %typed_value_t* %retval_p
}

define void @stack_push(%stack_t* %stack_p, %typed_value_t* %tv_p) {
  %size_p = getelementptr %stack_t* %stack_p, i64 0, i32 0
  %size = load i64* %size_p
  %new_size = add i64 %size, 1
  store i64 %new_size, i64* %size_p

  %store_ppp = getelementptr %stack_t* %stack_p, i64 0, i32 2
  %store_pp = load %typed_value_t*** %store_ppp
  %frame_pp = getelementptr %typed_value_t** %store_pp, i64 %new_size
  store %typed_value_t* %tv_p, %typed_value_t** %frame_pp
  
  ret void
}

define i32 @main() {
  call %typed_value_t* @function_abstract()
  %ok_str = getelementptr [4 x i8]* @ok, i64 0, i64 0
  call i32 @puts(i8* %ok_str)
  ret i32 0
}

