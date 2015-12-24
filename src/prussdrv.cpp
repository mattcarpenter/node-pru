//System headers
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <pthread.h>

//PRU Driver headers
#include <prussdrv.h>
#include <pruss_intc_mapping.h>	 
#define OFFSET_SHAREDRAM_DEFAULT 2048

#define X_INT		1
#define X_BYTE		2
#define Y_SHAREDRAM	1
#define Y_DATAMEM	2
#define M_GET		1
#define M_SET		2

//Node.js addon headers
#include <node.h>
#include <node_version.h>
#include <node_buffer.h>
#include <nan.h>

//shared memory pointer
static unsigned int* sharedMem_int;

//data memory pointers
static unsigned int* dataMem_pru0_int;
static unsigned int* dataMem_pru1_int;

//offset to be used
unsigned int offset_sharedRam = OFFSET_SHAREDRAM_DEFAULT;

NAN_METHOD(InitPRU);
NAN_METHOD(loadDatafile);
NAN_METHOD(executeProgram);
NAN_METHOD(setSharedRAMOffset);
NAN_METHOD(getSharedRAMOffset);
NAN_METHOD(getSharedRAM);
NAN_METHOD(setSharedRAM);
NAN_METHOD(getOrSetXFromOrToY);
NAN_METHOD(getSharedRAMInt);
NAN_METHOD(getSharedRAMByte);
NAN_METHOD(getDataRAMInt);
NAN_METHOD(getDataRAMByte);
NAN_METHOD(setShardRAMInt);
NAN_METHOD(setSharedRAMByte);
NAN_METHOD(setDataRAMInt);
NAN_METHOD(setDataRAMByte);
NAN_METHOD(clearInterrupt);
NAN_METHOD(interruptPRU);
NAN_METHOD(forceExit);

//using v8::FunctionTemplate;
//using v8::String;
//using v8::Object;
//using v8::Handle;
//using v8::Array;
//using v8::Local;
using namespace v8;

/* Initialise the PRU
 *	Initialise the PRU driver and static memory
 *	Takes no arguments and returns nothing
 */
NAN_METHOD(InitPRU) {
	
	//Initialise driver
	prussdrv_init ();
	
	//Open interrupt
	unsigned int ret = prussdrv_open(PRU_EVTOUT_0);
	if (ret) {
		return Nan::ThrowError("Could not open PRU driver. Did you forget to load device tree fragment?");
			
	}
	
	//Initialise interrupt
	tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;	
	prussdrv_pruintc_init(&pruss_intc_initdata);
	
	// Allocate shared PRU memory
    	prussdrv_map_prumem(PRUSS0_SHARED_DATARAM, (void **) &sharedMem_int);

	prussdrv_map_prumem(PRUSS0_PRU0_DATARAM, (void **) &dataMem_pru0_int);
	prussdrv_map_prumem(PRUSS0_PRU1_DATARAM, (void **) &dataMem_pru1_int);	
}

/* Loads PRU data file
 *
 */
NAN_METHOD(loadDatafile) {	
	Nan::HandleScope scope;

	int pruNum;
	
	if (info.Length() != 2) {
		return Nan::ThrowTypeError("Wrong number of arguments");
  	}

  	if (!info[0]->IsNumber()) {
  		return Nan::ThrowTypeError("Argument must be a number");
  	}
  	
  	if (!info[1]->IsString()) {
  		return Nan::ThrowTypeError("Argument must be a string");
  	}
  
  	//Get a C++ string
	String::Utf8Value datafile(info[1]->ToString());
	std::string datafileS = std::string(*datafile);

	//Get PRU num from arguments
	pruNum = info[0]->Int32Value();
	
	//Load the datafile
	int rc = prussdrv_load_datafile (pruNum, (char*)datafileS.c_str());
	if (rc != 0) {
		return Nan::ThrowTypeError("failed to load datafile");
	}
}

/* Execute PRU program
 *	Takes the filename of the .bin
 *	
 *	@param {number} PRU number
 *	@param {string} filename
 *	@param {number} address
 */
NAN_METHOD(executeProgram) {	
	Nan::HandleScope scope;

	size_t address = 0;
	int pruNum = 0;

	//Check we have three arguments
	if (info.Length() != 3) {
		return Nan::ThrowError("Wrong number of arguments");
	}

	if (info[2]->IsNumber()) {
		address = info[2]->Uint32Value();
	}

	//Get PRU number
	pruNum = info[0]->Int32Value();

	//Check that it's a string
	if (!info[1]->IsString()) {
		return Nan::ThrowError("Argument must be a string");
	}
	
	//Get a C++ string
	String::Utf8Value program(info[1]->ToString());
	std::string programS = std::string(*program);
	
	//Execute the program
	int rc = prussdrv_exec_program_at (pruNum, (char*)programS.c_str(), address);
	if (rc != 0) {
		return Nan::ThrowError("failed to execute PRU firmware");
	}
};


/* Set the shared PRU RAM offset to a user-defined value to override default
 *	Takes an integer as input, which is set as the new offset
 */
NAN_METHOD(setSharedRAMOffset) {
	Nan::HandleScope scope;	

	//Check we have single argument
	if (info.Length() != 1) {
		return Nan::ThrowTypeError("Wrong number of arguments");
	}

	//Check it's a number
	if (!info[0]->IsNumber()) {
		return Nan::ThrowTypeError("Argument must be Integer");
	}

	// set offset
	offset_sharedRam = (unsigned int)Array::Cast(*info[0])->NumberValue();
};

/* Get current shared PRU RAM offset
 *	Takes no arguments
 */
NAN_METHOD(getSharedRAMOffset) {
	Nan::HandleScope scope;
	info.GetReturnValue().Set(Nan::New<v8::Number>(offset_sharedRam));
};

/* Set the shared PRU RAM to an input array
 *	Takes an integer array as input, writes it to PRU shared memory
 *	Not much error checking here, don't pass in large arrays or seg faults will happen
 *	TODO: error checking and allow user to select range to set
 *  New: also accepts an index + Buffer object as arguments
 *  TODO: check if this usage of Buffers causes memory leaks
 */
NAN_METHOD(setSharedRAM) {
	Nan::HandleScope scope;
	unsigned int i;
	
	//Check we have a single argument
	if (!(info.Length() == 1 || info.Length() == 2)) {
		return Nan::ThrowTypeError("Wrong number of arguments");
	}
	
	//Check that it's an array or index and Buffer object
	if ((info.Length() == 1 && !info[0]->IsArray()) || (info.Length() == 2 && !(info[0]->IsNumber() && info[1]->IsObject()))) {
		return Nan::ThrowTypeError("Argument must be an array or an index and a Buffer object");
	}
	
	if (info[0]->IsArray()) {
		//Get array
		Local<Array> a = Local<Array>::Cast(info[0]);
		
		//Iterate over array
		for (i = 0; i < a->Length(); i++) {
			//Get element and check it's numeric
			Local<Value> element = a->Get(i);
			if (!element->IsNumber()) {
				return Nan::ThrowTypeError("Array must be integer");
			}
			
			//Set corresponding memory bytes
			sharedMem_int[offset_sharedRam + i] = (unsigned int) element->NumberValue();
		}
	} else {
		unsigned int index = info[0]->Uint32Value();
		
		//According to https://luismreis.github.io/node-bindings-guide/docs/arguments.html
		Local<Object> buf = info[1]->ToObject();
		char* data = node::Buffer::Data(buf);
		size_t data_length = node::Buffer::Length(buf);
		for (i = 0; i < data_length; i++) {
			((char*) (sharedMem_int + offset_sharedRam))[index + i] = data[i];
		}
	}
};


/* Get array from shared memory
 *	Returns first 16 integers from shared memory (legacy default)
 *  New: Accepts start index and length as parameters and returns an actual Node Buffer
 *  TODO: check if this usage of Buffers causes memory leaks
 */
NAN_METHOD(getSharedRAM) {
	Nan::HandleScope scope;
	
	if (info.Length() < 1) { // for legacy compatibility
		//Create output array
		Local<Array> a = Nan::New<Array>(16);
		
		//Iterate over output array and fill it with shared memory data
		for (unsigned int i = 0; i<a->Length(); i++) {
			a->Set(i,Nan::New<Number>(sharedMem_int[offset_sharedRam + i]));
		}
		
		//Return array
		return info.GetReturnValue().Set(a);
	} else {
		if (info.Length() != 2) {
			return Nan::ThrowTypeError("Wrong number of arguments");
		}
		
		//Check they are both numbers
		if (!info[0]->IsNumber() || !info[1]->IsNumber()) {
			return Nan::ThrowTypeError("Arguments must be Integer");
		}
		
		//Get the numbers
		unsigned int index = (unsigned short)Array::Cast(*info[0])->NumberValue();
		unsigned int length = (unsigned int)Array::Cast(*info[1])->NumberValue();
		
		Nan::MaybeLocal<v8::Object> buf = Nan::CopyBuffer(reinterpret_cast<const char*>(sharedMem_int + index), length);	
		info.GetReturnValue().Set(buf.ToLocalChecked());
	}
};


/* Get single integer or byte from shared or data memory
 *	Takes integer index argument, returns Number at that index
 *  Careful: index for int and byte will differ! index for int == index for byte / 4 (simplified)
 *  Legacy considerations: execute() above takes the PRU num as the first argument, but since
 *  previously getDataRAMInt defaulted to PRU num 1 we don't want to break programs. Therefore,
 *  the PRU num stays optional and defaults to 0.
 */
Local<Value> getOrSetXFromOrToY(char mode, char what, char where, Nan::NAN_METHOD_ARGS_TYPE args) {	//array
	Nan::HandleScope scope;
	int pruNum = 0;
	unsigned int val = 0;
	const char maxArgs = (mode == M_GET)? 2 : 3;
		
	//Check we have at least one argument
	if (args.Length() < 1 || args.Length() > maxArgs) {
		Nan::ThrowTypeError("Wrong number of arguments");
		return Nan::Null();
	}
	
	//Check if arguments are numbers
	if 	(!args[0]->IsNumber() || (args.Length() > 1 && !args[1]->IsNumber()) || (args.Length() > 2 && !args[2]->IsNumber())) {
		Nan::ThrowTypeError("Argument must be Integer");
		return Nan::Null();
	}
	
	if (mode == M_SET) {
		if (what == X_INT) {
			val = (where == Y_DATAMEM && args.Length() > 2)? (unsigned int)Array::Cast(*args[2])->NumberValue() : (unsigned int)Array::Cast(*args[1])->NumberValue();
		} else {
			val = (where == Y_DATAMEM && args.Length() > 2)? (unsigned char)Array::Cast(*args[2])->NumberValue() : (unsigned char)Array::Cast(*args[1])->NumberValue();
		}
	}
	
	//Get index value
	unsigned short index;
	if (where == Y_DATAMEM && args.Length() > 1) {
		index = (unsigned short)Array::Cast(*args[1])->NumberValue();
		pruNum = args[0]->Int32Value();
	} else {
		index = (unsigned short)Array::Cast(*args[0])->NumberValue();
	}
	
	unsigned int* addr;
	if (where == Y_DATAMEM) {
		if (pruNum == 0) {
			addr = dataMem_pru0_int;
		} else {
			addr = dataMem_pru1_int;
		}
	} else {
		addr = sharedMem_int + offset_sharedRam;
	}
	
	if (what == X_INT) {
		if (mode == M_SET) {
			addr[index] = val;
		}
		return Nan::New<v8::Number>(addr[index]);
	} else {
		if (mode == M_SET) {
			((unsigned char*) addr)[index] = val;
		}
		return Nan::New<v8::Number>(((unsigned char*) addr)[index]);
	}
};

NAN_METHOD(getSharedRAMInt) {
	info.GetReturnValue().Set(getOrSetXFromOrToY(M_GET, X_INT, Y_SHAREDRAM, info));
};

NAN_METHOD(getSharedRAMByte) {
	info.GetReturnValue().Set(getOrSetXFromOrToY(M_GET, X_BYTE, Y_SHAREDRAM, info));
};

NAN_METHOD(getDataRAMInt) {
	info.GetReturnValue().Set(getOrSetXFromOrToY(M_GET, X_INT, Y_DATAMEM, info));
};

NAN_METHOD(getDataRAMByte) {
	info.GetReturnValue().Set(getOrSetXFromOrToY(M_GET, X_BYTE, Y_DATAMEM, info));
};

NAN_METHOD(setSharedRAMInt) {
	info.GetReturnValue().Set(getOrSetXFromOrToY(M_SET, X_INT, Y_SHAREDRAM, info));
};

NAN_METHOD(setSharedRAMByte) {
	info.GetReturnValue().Set(getOrSetXFromOrToY(M_SET, X_BYTE, Y_SHAREDRAM, info));
};

NAN_METHOD(setDataRAMInt) {
	info.GetReturnValue().Set(getOrSetXFromOrToY(M_SET, X_INT, Y_DATAMEM, info));
};

NAN_METHOD(setDataRAMByte) {
	info.GetReturnValue().Set(getOrSetXFromOrToY(M_SET, X_BYTE, Y_DATAMEM, info));
};

/*-------------------This is mostly copy/pasted from here: ---------------------*/
/*----------------http://kkaefer.github.io/node-cpp-modules/--------------------*/
struct Baton {
    uv_work_t request;
    Nan::Persistent<Function> callback;
    int error_code;
    std::string error_message;
    int32_t result;
};

void AsyncWork(uv_work_t* req) {
//    Baton* baton = static_cast<Baton*>(req->data);
	prussdrv_pru_wait_event(PRU_EVTOUT_0);
}

// fix for "warning: invalid conversion from void (*)(uv_work_t*) {aka void (*)(uv_work_s*)} to uv_after_work_cb {aka void (*)(uv_work_s*, int)}"
// modelled after https://github.com/santigimeno/node-pcsclite/commit/194351d13dc3ee6f506bfc9d4b49244b6b318a12
#if NODE_VERSION_AT_LEAST(0, 9, 4)
	void AsyncAfter(uv_work_t* req, int status) {
#else
	void AsyncAfter(uv_work_t* req) {
#endif
    Nan::HandleScope scope;
    Baton* baton = static_cast<Baton*>(req->data);
    Local<Function> cb = Nan::New<Function>(baton->callback);
    cb->Call(Nan::GetCurrentContext()->Global(), 0, 0);
    baton->callback.Reset();
    delete baton;
}

NAN_METHOD(waitForInterrupt) {
	Nan::HandleScope scope;
	Local<Function> callback = Local<Function>::Cast(info[0]);

	Baton* baton = new Baton();
        baton->request.data = baton;
        baton->callback.Reset(callback);	
	uv_queue_work(uv_default_loop(), &baton->request, AsyncWork, AsyncAfter);
}

/*---------------------------Here ends the copy/pasting----------------------------*/

/* Clear Interrupt */
NAN_METHOD(clearInterrupt) {
	Nan::HandleScope scope;
	
	//Check we have single argument
	if (info.Length() != 1) {
		return Nan::ThrowTypeError("Wrong number of arguments");
	}
	
	//Check it's a number
	if (!info[0]->IsNumber()) {
		return Nan::ThrowTypeError("Argument must be Integer");
	}
	
	//Get index value
	int event = (int) Array::Cast(*info[0])->NumberValue();
	
	prussdrv_pru_clear_event(PRU0_ARM_INTERRUPT, event);
};

NAN_METHOD(interruptPRU) {
	Nan::HandleScope scope;
	prussdrv_pru_send_event(ARM_PRU0_INTERRUPT);
};


/* Force the PRU code to terminate */
NAN_METHOD(forceExit) {
	Nan::HandleScope scope;
	if (info.Length() != 1) {
		return Nan::ThrowTypeError("Wrong number of arguments");
	}

	prussdrv_pru_disable(info[0]->Uint32Value()); 
    	prussdrv_exit();
};

/* Initialise the module */
NAN_MODULE_INIT(Init) {
	//	pru.init();
	Nan::Set(target, Nan::New("init").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(InitPRU)).ToLocalChecked());

	//	pru.loadDatafile(0, "data.bin");
	Nan::Set(target, Nan::New("loadDataFile").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(loadDatafile)).ToLocalChecked());
	
	//	pru.execute(0, "mycode.bin", 0x40);
	Nan::Set(target, Nan::New("execute").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(executeProgram)).ToLocalChecked());
	
	//	var intVal = pru.getSharedRAMOffset();
	Nan::Set(target, Nan::New("getSharedRAMOffset").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(getSharedRAMOffset)).ToLocalChecked());
	
	//	pru.setSharedRAMOffset(0x100);
	Nan::Set(target, Nan::New("setSharedRAMOffset").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(setSharedRAMOffset)).ToLocalChecked());
	
	// var intArray = pru.getSharedRAM();
	// or: var myBuffer = pru.getSharedRAM(4, 12); // returns Buffer with 12 bytes
	Nan::Set(target, Nan::New("getSharedRAM").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(getSharedRAM)).ToLocalChecked());
	
	//	pru.setSharedRAM([0x1, 0x2, 0x3]);
	Nan::Set(target, Nan::New("setSharedRAM").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(setSharedRAM)).ToLocalChecked());
	
	//	var intVal = pru.getSharedRAMInt(3);
	Nan::Set(target, Nan::New("getSharedRAMInt").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(getSharedRAMInt)).ToLocalChecked());

	//	var intVal = pru.getDataRAMInt(3);
	// or: var intVal = pru.getDataRAMInt(1, 4); // first arg is the PRU num
	Nan::Set(target, Nan::New("getDataRAMInt").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(getDataRAMInt)).ToLocalChecked());
	
	//	var byteVal = pru.getSharedRAMByte(3);
	Nan::Set(target, Nan::New("getSharedRAMByte").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(getSharedRAMByte)).ToLocalChecked());
	
	//	var byteVal = pru.getDataRAMByte(3);
	// or: var byteVal = pru.getDataRAMByte(1, 4); // first arg is the PRU num
	Nan::Set(target, Nan::New("getDataRAMByte").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(getDataRAMByte)).ToLocalChecked());;

	//	pru.setSharedRAMInt(4, 0xa1b2c3d4);
	Nan::Set(target, Nan::New("setSharedRAMInt").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(setSharedRAMInt)).ToLocalChecked());
	
	//	pru.setDataRAMInt(4, 0xa1b2c3d4);
	// or: pru.setDataRAMInt(1, 4, 0xa1b2c3d4); // first arg is the PRU num
	Nan::Set(target, Nan::New("setDataRAMInt").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(setDataRAMInt)).ToLocalChecked());
	
	//	pru.setSharedRAMByte(4, 0xab);
	Nan::Set(target, Nan::New("setSharedRAMByte").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(setSharedRAMByte)).ToLocalChecked());

	//	pru.setDataRAMByte(4, 0xff);
	// or: pru.setDataRAMByte(1, 4, 0xff); // first arg is the PRU num
	Nan::Set(target, Nan::New("setDataRAMByte").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(setDataRAMByte)).ToLocalChecked());
	
	//	pru.waitForInterrupt(function() { console.log("Interrupted by PRU");});
	Nan::Set(target, Nan::New("waitForInterrupt").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(waitForInterrupt)).ToLocalChecked());

	//	pru.clearInterrupt();
	Nan::Set(target, Nan::New("clearInterrupt").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(clearInterrupt)).ToLocalChecked());
	
	//	pru.interrupt();
	Nan::Set(target, Nan::New("interrupt").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(interruptPRU)).ToLocalChecked());
	
	//	pru.exit();
	Nan::Set(target, Nan::New("exit").ToLocalChecked(),
		Nan::GetFunction(Nan::New<FunctionTemplate>(forceExit)).ToLocalChecked());
}

NODE_MODULE(prussdrv, Init)
