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
			});;
		}
	return obj;
}

module.exports = {
	"create_node": create_node,
	"NMT_OPERATIONAL": 0x01,
	"NMT_STOP": 0x02,
	"NMT_PRE_OPERATIONAL": 0x80,
	"NMT_RESET_NODE": 0x81,
	"NMT_RESET_COMMUNICATION": 0x82
};

