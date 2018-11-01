dco = require('./build/Release/dcanopen');

function create_node(device, node_id){
	var obj = dco.create_node(device, node_id);
	obj.sdo_download_array = function (index, subindex, array){
			return new Promise(function(resolve, reject) {
				obj.sdo_download(index, subindex, array, res =>{
					if(res instanceof Error) reject(res);
					else resolve(res);
				});
			});
		}
	obj.sdo_download_uint8 = function (index, subindex, number){
			var data = new ArrayBuffer(1);
			var v8 = new Uint8Array(data);
			v8[0] = number;
			return new Promise(function(resolve, reject) {
				obj.sdo_download(index, subindex, data, res =>{
					if(res instanceof Error) reject(res);
					else resolve(res);
				});
			});
		}
	obj.sdo_download_uint16 = function (index, subindex, number){
			var data = new ArrayBuffer(2);
			var v16 = new Uint16Array(data);
			v16[0] = number;
			return new Promise(function(resolve, reject) {
				obj.sdo_download(index, subindex, data, res =>{
					if(res instanceof Error) reject(res);
					else resolve(res);
				});
			});
		}
	obj.sdo_download_uint32 = function (index, subindex, number){
			var data = new ArrayBuffer(4);
			var v32 = new Uint32Array(data);
			v32[0] = number;
			return new Promise(function(resolve, reject) {
				obj.sdo_download(index, subindex, data, res =>{
					if(res instanceof Error) reject(res);
					else resolve(res);
				});
			});
		}
	obj.sdo_upload_array = function (index, subindex){
			return new Promise(function(resolve, reject) {
				obj.sdo_upload(index, subindex, res =>{
					if(res instanceof Error) reject(res);
					else resolve(res);
				});
			});
		}
	obj.sdo_upload_uint8 = function (index, subindex){
			return new Promise(function(resolve, reject) {
				obj.sdo_upload(index, subindex, res =>{
					if(res instanceof Error) reject(res);
					else resolve(new Uint8Array(res)[0]);
				});
			});
		}
	obj.sdo_upload_uint16 = function (index, subindex, cb){
			return new Promise(function(resolve, reject) {
				obj.sdo_upload(index, subindex, res =>{
					if(res instanceof Error) reject(res);
					else resolve(new Uint16Array(res)[0]);
				});
			});
		}
	obj.sdo_upload_uint32 = function (index, subindex, cb){
			return new Promise(function(resolve, reject) {
				obj.sdo_upload(index, subindex, res =>{
					if(res instanceof Error) reject(res);
					else resolve(new Uint32Array(res)[0]);
				});
			});
		}
	obj.heartbeat_str = function (cb){
			obj.heartbeat(function(state){
				if(state instanceof Error) cb(state);
				else switch(state){
					case dco.HB_BOOT:
						cb("BOOT");
						break;
					case dco.HB_STOPPED:
						cb("STOPPED");
						break;
					case dco.HB_OPERATIONAL:
						cb("OPERATIONAL");
						break;
					case dco.HB_PRE_OPERATIONAL:
						cb("PRE_OPERATIONAL");
						break;
					default:
						cb(state);
				}
			});
		}
	return obj;
}

module.exports = {
	"create_node": create_node,
	"NMT_OPERATIONAL": dco.NMT_OPERATIONAL,
	"NMT_STOP": dco.NMT_STOP,
	"NMT_PRE_OPERATIONAL": dco.NMT_PRE_OPERATIONAL,
	"NMT_RESET_NODE": dco.NMT_RESET_NODE,
	"NMT_RESET_COMMUNICATION": dco.NMT_RESET_COMMUNICATION,
	"HB_BOOT": dco.HB_BOOT,
	"HB_STOPPED": dco.HB_STOPPED,
	"HB_OPERATIONAL": dco.HB_OPERATIONAL,
	"HB_PRE_OPERATIONAL": dco.HB_PRE_OPERATIONAL
};

