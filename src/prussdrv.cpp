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
#include <v8.h>

using namespace v8;

//shared memory pointer
static unsigned int* sharedMem_int;

//data memory pointers
static unsigned int* dataMem_pru0_int;
static unsigned int* dataMem_pru1_int;

//offset to be used
unsigned int offset_sharedRam = OFFSET_SHAREDRAM_DEFAULT;

/* Initialise the PRU
 *	Initialise the PRU driver and static memory
 *	Takes no arguments and returns nothing
 */
Handle<Value> InitPRU(const Arguments& args) {
	HandleScope scope;
	
	//Initialise driver
	prussdrv_init ();
	
	//Open interrupt
	unsigned int ret = prussdrv_open(PRU_EVTOUT_0);
	if (ret) {
		ThrowException(Exception::Error(String::New("Could not open PRU driver. Did you forget to load device tree fragment?")));
		return scope.Close(Undefined());
	}
	
	//Initialise interrupt
	tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;	
	prussdrv_pruintc_init(&pruss_intc_initdata);
	
	// Allocate shared PRU memory
    	prussdrv_map_prumem(PRUSS0_SHARED_DATARAM, (void **) &sharedMem_int);

	prussdrv_map_prumem(PRUSS0_PRU0_DATARAM, (void **) &dataMem_pru0_int);
	prussdrv_map_prumem(PRUSS0_PRU1_DATARAM, (void **) &dataMem_pru1_int);
	
	//Return nothing
	return scope.Close(Undefined());
}

/* Loads PRU data file
 *
 */
Handle<Value> loadDatafile(const Arguments& args) {
	HandleScope scope;
	int pruNum;
	
	if (args.Length() != 2) {
		ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
  		return scope.Close(Undefined());
  	}

  	if (!args[0]->IsNumber()) {
  		ThrowException(Exception::TypeError(String::New("Argument must be a number")));
  		return scope.Close(Undefined());
  	}
  	
  	if (!args[1]->IsString()) {
  		ThrowException(Exception::TypeError(String::New("Argument must be a string")));
  		return scope.Close(Undefined());
  	}
  
  	//Get a C++ string
	String::Utf8Value datafile(args[1]->ToString());
	std::string datafileS = std::string(*datafile);

	//Get PRU num from arguments
	pruNum = args[0]->Int32Value();
	
	//Load the datafile
	int rc = prussdrv_load_datafile (pruNum, (char*)datafileS.c_str());
	if (rc != 0) {
		ThrowException(Exception::TypeError(String::New("failed to load datafile")));
		return scope.Close(Undefined());
	}

	return scope.Close(Undefined());
}

/* Execute PRU program
 *	Takes the filename of the .bin
 *	
 *	@param {number} PRU number
 *	@param {string} filename
 *	@param {number} address
 */
Handle<Value> executeProgram(const Arguments& args) {
	HandleScope scope;
	size_t address = 0;
	int pruNum = 0;

	//Check we have three arguments
	if (args.Length() != 3) {
		ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
		return scope.Close(Undefined());
	}

	if (args[2]->IsNumber()) {
		address = args[2]->Uint32Value();
	}

	//Get PRU number
	pruNum = args[0]->Int32Value();

	//Check that it's a string
	if (!args[1]->IsString()) {
		ThrowException(Exception::TypeError(String::New("Argument must be a string")));
		return scope.Close(Undefined());
	}
	
	//Get a C++ string
	String::Utf8Value program(args[1]->ToString());
	std::string programS = std::string(*program);
	
	//Execute the program
	int rc = prussdrv_exec_program_at (pruNum, (char*)programS.c_str(), address);
	if (rc != 0) {
		ThrowException(Exception::TypeError(String::New("failed to execute PRU firmware")));
		return scope.Close(Undefined());
	}
	
	//Return nothing
	return scope.Close(Undefined());
};


/* Set the shared PRU RAM offset to a user-defined value to override default
 *	Takes an integer as input, which is set as the new offset
 */
Handle<Value> setSharedRAMOffset(const Arguments& args) {	//array
	HandleScope scope;
	
	//Check we have single argument
	if (args.Length() != 1) {
		ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
		return scope.Close(Undefined());
	}

	//Check it's a number
	if (!args[0]->IsNumber()) {
		ThrowException(Exception::TypeError(String::New("Argument must be Integer")));
		return scope.Close(Undefined());
	}

	// set offset
	offset_sharedRam = (unsigned int)Array::Cast(*args[0])->NumberValue();
	//Return nothing
	return scope.Close(Undefined());
};

/* Get current shared PRU RAM offset
 *	Takes no arguments
 */
Handle<Value> getSharedRAMOffset(const Arguments& args) {	//array
	HandleScope scope;
	return scope.Close(Number::New(offset_sharedRam));
};

/* Set the shared PRU RAM to an input array
 *	Takes an integer array as input, writes it to PRU shared memory
 *	Not much error checking here, don't pass in large arrays or seg faults will happen
 *	TODO: error checking and allow user to select range to set
 *  New: also accepts an index + Buffer object as arguments
 *  TODO: check if this usage of Buffers causes memory leaks
 */
Handle<Value> setSharedRAM(const Arguments& args) {
	HandleScope scope;
	unsigned int i;
	
	//Check we have a single argument
	if (!(args.Length() == 1 || args.Length() == 2)) {
		ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
		return scope.Close(Undefined());
	}
	
	//Check that it's an array or index and Buffer object
	if ((args.Length() == 1 && !args[0]->IsArray()) || (args.Length() == 2 && !(args[0]->IsNumber() && args[1]->IsObject()))) {
		ThrowException(Exception::TypeError(String::New("Argument must be an array or an index and a Buffer object")));
		return scope.Close(Undefined());
	}
	
	if (args[0]->IsArray()) {
		//Get array
		Local<Array> a = Array::Cast(*args[0]);
		
		//Iterate over array
		for (i = 0; i<a->Length(); i++) {
			//Get element and check it's numeric
			Local<Value> element = a->Get(i);
			if (!element->IsNumber()) {
				ThrowException(Exception::TypeError(String::New("Array must be integer")));
				return scope.Close(Undefined());
			}
			
			//Set corresponding memory bytes
			sharedMem_int[offset_sharedRam + i] = (unsigned int) element->NumberValue();
		}
	} else {
		unsigned int index = args[0]->Uint32Value();
		
		//According to https://luismreis.github.io/node-bindings-guide/docs/arguments.html
		Local<Object> buf = args[1]->ToObject();
		char* data = node::Buffer::Data(buf);
		size_t data_length = node::Buffer::Length(buf);
		for (i = 0; i < data_length; i++) {
			((char*) (sharedMem_int + offset_sharedRam))[index + i] = data[i];
		}
	}
	//Return nothing
	return scope.Close(Undefined());
};


/* Get array from shared memory
 *	Returns first 16 integers from shared memory (legacy default)
 *  New: Accepts start index and length as parameters and returns an actual Node Buffer
 *  TODO: check if this usage of Buffers causes memory leaks
 */
Handle<Value> getSharedRAM(const Arguments& args) {	//array
	HandleScope scope;
	
	if (args.Length() < 1) { // for legacy compatibility
		//Create output array
		Local<Array> a = Array::New(16);
		
		//Iterate over output array and fill it with shared memory data
		for (unsigned int i = 0; i<a->Length(); i++) {
			a->Set(i,Number::New(sharedMem_int[offset_sharedRam + i]));
		}
		
		//Return array
		return scope.Close(a);
	} else {
		if (args.Length() != 2) {
			ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
			return scope.Close(Undefined());
		}
		
		//Check they are both numbers
		if (!args[0]->IsNumber() || !args[1]->IsNumber()) {
			ThrowException(Exception::TypeError(String::New("Arguments must be Integer")));
			return scope.Close(Undefined());
		}
		
		//Get the numbers
		unsigned int index = (unsigned short)Array::Cast(*args[0])->NumberValue();
		unsigned int length = (unsigned int)Array::Cast(*args[1])->NumberValue();
		
		//Props for showing how to use Buffers from CPP: http://www.samcday.com.au/blog/2011/03/03/creating-a-proper-buffer-in-a-node-c-addon/
		node::Buffer *buf = node::Buffer::New(length);
		memcpy(node::Buffer::Data(buf), sharedMem_int + index, length);
		
		Local<Object> globalObj = Context::GetCurrent()->Global();
		Local<Function> bufferConstructor = Local<Function>::Cast(globalObj->Get(String::New("Buffer")));
		Handle<Value> constructorArgs[3] = {
			buf->handle_,
			Integer::New(length),
			Integer::New(0)
		};
		Local<Object> actualBuf = bufferConstructor->NewInstance(3, constructorArgs);
		return scope.Close(actualBuf);
	}
};


/* Get single integer or byte from shared or data memory
 *	Takes integer index argument, returns Number at that index
 *  Careful: index for int and byte will differ! index for int == index for byte / 4 (simplified)
 *  Legacy considerations: execute() above takes the PRU num as the first argument, but since
 *  previously getDataRAMInt defaulted to PRU num 1 we don't want to break programs. Therefore,
 *  the PRU num stays optional and defaults to 0.
 */
Handle<Value> getOrSetXFromOrToY(char mode, char what, char where, const Arguments& args) {	//array
	HandleScope scope;
	int pruNum = 0;
	unsigned int val = 0;
	const char maxArgs = (mode == M_GET)? 2 : 3;
	
	//Check we have at least one argument
	if (args.Length() < 1 || args.Length() > maxArgs) {
		ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
		return scope.Close(Undefined());
	}
	
	//Check if arguments are numbers
	if 	(!args[0]->IsNumber() || (args.Length() > 1 && !args[1]->IsNumber()) || (args.Length() > 2 && !args[2]->IsNumber())) {
		ThrowException(Exception::TypeError(String::New("Argument must be Integer")));
		return scope.Close(Undefined());
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
		return scope.Close(Number::New(addr[index]));
	} else {
		if (mode == M_SET) {
			((unsigned char*) addr)[index] = val;
		}
		return scope.Close(Number::New(((unsigned char*) addr)[index]));
	}
};

Handle<Value> getSharedRAMInt(const Arguments& args) {
	return getOrSetXFromOrToY(M_GET, X_INT, Y_SHAREDRAM, args);
};

Handle<Value> getSharedRAMByte(const Arguments& args) {
	return getOrSetXFromOrToY(M_GET, X_BYTE, Y_SHAREDRAM, args);
};

Handle<Value> getDataRAMInt(const Arguments& args) {
	return getOrSetXFromOrToY(M_GET, X_INT, Y_DATAMEM, args);
};

Handle<Value> getDataRAMByte(const Arguments& args) {
	return getOrSetXFromOrToY(M_GET, X_BYTE, Y_DATAMEM, args);
};

Handle<Value> setSharedRAMInt(const Arguments& args) {
	return getOrSetXFromOrToY(M_SET, X_INT, Y_SHAREDRAM, args);
};

Handle<Value> setSharedRAMByte(const Arguments& args) {
	return getOrSetXFromOrToY(M_SET, X_BYTE, Y_SHAREDRAM, args);
};

Handle<Value> setDataRAMInt(const Arguments& args) {
	return getOrSetXFromOrToY(M_SET, X_INT, Y_DATAMEM, args);
};

Handle<Value> setDataRAMByte(const Arguments& args) {
	return getOrSetXFromOrToY(M_SET, X_BYTE, Y_DATAMEM, args);
};

/*-------------------This is mostly copy/pasted from here: ---------------------*/
/*----------------http://kkaefer.github.io/node-cpp-modules/--------------------*/
struct Baton {
    uv_work_t request;
    Persistent<Function> callback;
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
    HandleScope scope;
    Baton* baton = static_cast<Baton*>(req->data);
	baton->callback->Call(Context::GetCurrent()->Global(), 0, 0);
    baton->callback.Dispose();
    delete baton;
}

Handle<Value> waitForInterrupt(const Arguments& args) {
	HandleScope scope;
	Local<Function> callback = Local<Function>::Cast(args[0]);

	Baton* baton = new Baton();
    baton->request.data = baton;
    baton->callback = Persistent<Function>::New(callback);
	
	uv_queue_work(uv_default_loop(), &baton->request, AsyncWork, AsyncAfter);
	return scope.Close(Undefined());
}

/*---------------------------Here ends the copy/pasting----------------------------*/

/* Clear Interrupt */
Handle<Value> clearInterrupt(const Arguments& args) {
	HandleScope scope;
	
	//Check we have single argument
	if (args.Length() != 1) {
		ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
		return scope.Close(Undefined());
	}
	
	//Check it's a number
	if (!args[0]->IsNumber()) {
		ThrowException(Exception::TypeError(String::New("Argument must be Integer")));
		return scope.Close(Undefined());
	}
	
	//Get index value
	int event = (int) Array::Cast(*args[0])->NumberValue();
	
	prussdrv_pru_clear_event(PRU0_ARM_INTERRUPT, event);
	return scope.Close(Undefined());
};

Handle<Value> interruptPRU(const Arguments& args) {
	HandleScope scope;
	prussdrv_pru_send_event(ARM_PRU0_INTERRUPT);
	return scope.Close(Undefined());
};


/* Force the PRU code to terminate */
Handle<Value> forceExit(const Arguments& args) {
	HandleScope scope;
	if (args.Length() != 1) {
		ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
		return scope.Close(Undefined());
	}

	prussdrv_pru_disable(args[0]->Uint32Value()); 
    prussdrv_exit ();
	return scope.Close(Undefined());
};

/* Initialise the module */
void Init(Handle<Object> exports, Handle<Object> module) {
	//	pru.init();
	exports->Set(String::NewSymbol("init"), FunctionTemplate::New(InitPRU)->GetFunction());

	//	pru.loadDatafile(0, "data.bin");
	exports->Set(String::NewSymbol("loadDatafile"), FunctionTemplate::New(loadDatafile)->GetFunction());
	
	//	pru.execute(0, "mycode.bin", 0x40);
	exports->Set(String::NewSymbol("execute"), FunctionTemplate::New(executeProgram)->GetFunction());
	
	//	var intVal = pru.getSharedRAMOffset();
	exports->Set(String::NewSymbol("getSharedRAMOffset"), FunctionTemplate::New(getSharedRAMOffset)->GetFunction());
	
	//	pru.setSharedRAMOffset(0x100);
	exports->Set(String::NewSymbol("setSharedRAMOffset"), FunctionTemplate::New(setSharedRAMOffset)->GetFunction());
	
	// var intArray = pru.getSharedRAM();
	// or: var myBuffer = pru.getSharedRAM(4, 12); // returns Buffer with 12 bytes
	exports->Set(String::NewSymbol("getSharedRAM"), FunctionTemplate::New(getSharedRAM)->GetFunction());
	
	//	pru.setSharedRAM([0x1, 0x2, 0x3]);
	exports->Set(String::NewSymbol("setSharedRAM"), FunctionTemplate::New(setSharedRAM)->GetFunction());
	
	//	var intVal = pru.getSharedRAMInt(3);
	exports->Set(String::NewSymbol("getSharedRAMInt"), FunctionTemplate::New(getSharedRAMInt)->GetFunction());

	//	var intVal = pru.getDataRAMInt(3);
	// or: var intVal = pru.getDataRAMInt(1, 4); // first arg is the PRU num
	exports->Set(String::NewSymbol("getDataRAMInt"), FunctionTemplate::New(getDataRAMInt)->GetFunction());
	
	//	var byteVal = pru.getSharedRAMByte(3);
	exports->Set(String::NewSymbol("getSharedRAMByte"), FunctionTemplate::New(getSharedRAMByte)->GetFunction());
	
	//	var byteVal = pru.getDataRAMByte(3);
	// or: var byteVal = pru.getDataRAMByte(1, 4); // first arg is the PRU num
	exports->Set(String::NewSymbol("getDataRAMByte"), FunctionTemplate::New(getDataRAMByte)->GetFunction());

	//	pru.setSharedRAMInt(4, 0xa1b2c3d4);
	exports->Set(String::NewSymbol("setSharedRAMInt"), FunctionTemplate::New(setSharedRAMInt)->GetFunction());
	
	//	pru.setDataRAMInt(4, 0xa1b2c3d4);
	// or: pru.setDataRAMInt(1, 4, 0xa1b2c3d4); // first arg is the PRU num
	exports->Set(String::NewSymbol("setDataRAMInt"), FunctionTemplate::New(setDataRAMInt)->GetFunction());
	
	//	pru.setSharedRAMByte(4, 0xab);
	exports->Set(String::NewSymbol("setSharedRAMByte"), FunctionTemplate::New(setSharedRAMByte)->GetFunction());
	
	//	pru.setDataRAMByte(4, 0xff);
	// or: pru.setDataRAMByte(1, 4, 0xff); // first arg is the PRU num
	exports->Set(String::NewSymbol("setDataRAMByte"), FunctionTemplate::New(setDataRAMByte)->GetFunction());
	
	//	pru.waitForInterrupt(function() { console.log("Interrupted by PRU");});
	exports->Set(String::NewSymbol("waitForInterrupt"), FunctionTemplate::New(waitForInterrupt)->GetFunction());

	//	pru.clearInterrupt();
	exports->Set(String::NewSymbol("clearInterrupt"), FunctionTemplate::New(clearInterrupt)->GetFunction());
	
	//	pru.interrupt();
	exports->Set(String::NewSymbol("interrupt"), FunctionTemplate::New(interruptPRU)->GetFunction());	
	
	//	pru.exit();
	exports->Set(String::NewSymbol("exit"), FunctionTemplate::New(forceExit)->GetFunction());
}

NODE_MODULE(prussdrv, Init)
