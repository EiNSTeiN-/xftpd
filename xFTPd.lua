--[[
   Copyright (c) 2007, The xFTPd Project.
   All rights reserved.
  
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
  
       * Redistributions of source code must retain the above copyright
         notice, this list of conditions and the following disclaimer.
  
       * Redistributions in binary form must reproduce the above copyright
         notice, this list of conditions and the following disclaimer in the
         documentation and/or other materials provided with the distribution.
  
       * Neither the name of the xFTPd Project nor the
         names of its contributors may be used to endorse or promote products
         derived from this software without specific prior written permission.
  
       * Redistributions of this project or parts of this project in any form
         must retain the following aknowledgment:
         "This product includes software developed by the xFTPd Project.
          http://www.xftpd.com/ - http://www.xftpd.org/"
  
   THIS SOFTWARE IS PROVIDED BY THE xFTPd PROJECT ``AS IS'' AND ANY
   EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE xFTPd PROJECT BE LIABLE FOR ANY
   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
]]

function printf(...)
	io.write(string.format(unpack(arg)));
end

-- time globals
SECOND = (1000);
MINUTE = (SECOND * 60);
HOUR = (MINUTE * 60);
DAY = (HOUR * 24);
WEEK = (DAY * 7);
MONTH = (WEEK * 4);
YEAR = (MONTH * 12);

-- size globals
KB = 1024;
MB = (KB * 1024);
GB = (MB * 1024);
TB = (GB * 1024);


--------------------------------------
-- APIs for the collection module
function casted_iteration(state, ctx)

	o, ctx.n = collection.iterate(state, ctx.n);
	if not o then
		setmetatable(ctx, {__mode = "v"});
		return nil, nil;
	end

	o = tolua.cast(o, ctx.type);
	if not o then
		setmetatable(ctx, {__mode = "v"});
		return nil, nil;
	end

	return ctx, o;
end

-- make something like below possible:
--
--	for k, item in casted(collection, "usertype_to_cast") do
--		-- item can now be used as
--		-- a "usertype_to_cast" object type
--	end
function casted(state, type)
	return casted_iteration, state, { n = 0, type = type };
end


--------------------------------------
-- APIs for the underlying skin system

OPEN_SKIN_FILE = "./skins/default.skin";
local xftpd_skin_config = nil;

-- set the skin file to use, wich is "default.skin" by default
function skin_open(name)

	-- TODO: make sure the file exists
	
	if(xftpd_skin_config) then
		config.destroy(xftpd_skin_config);
		xftpd_skin_config = nil;
	end
	
	OPEN_SKIN_FILE = name;
	xftpd_skin_config = config.new(OPEN_SKIN_FILE, 0);
	assert(xftpd_skin_config, "FATAL ERROR, CANNOT LOAD SKIN CONFIG FILE!");
end

skin_open(OPEN_SKIN_FILE);

-- set the contents of "line" in the current skin file
function skin_set(line, contents)


end

-- return the contents of "line" from the current skin file
function skin_get(line)


end

-- build a default skin table to be used
function skintable()

	return { b = "\002", c = "\003" };
end

function skinned_get_object_at(_table, v)
	local obj = _table;
	local _s = v;
	while _s do
		local _a, _b = nil, nil;
		n,n, _a,_b = string.find(_s, "(.-)%.(.+)");
		if not _a or not _b then
			_a = _s;
			_b = nil;
		end
		_s = _b;
		obj = obj[_a];
		if not obj then
			return "$".. v;
		end
	end
	return obj;
end

-- global table shared between the skinned()
-- function and the loaded chunks
skinned_global_table = nil;

-- find "line" in the skin file and gsub its content with "table"
function skinned(line, _table)
	local last_table = skinned_global_table;
	local contents = nil;

	skinned_global_table = _table;

	contents = config.read(xftpd_skin_config, line);
	
	if not contents then
		return line .. " was not found in " .. OPEN_SKIN_FILE;
	end
	
	contents = string.gsub(contents, "%@(.-)%@",
		function(v)
			local var = config.read(xftpd_skin_config, v);
			if not var then var = ""; end
			return var;
		end
	);

	contents = string.gsub(contents, "%&([%a%._]+)",
		function(v)
			return "skinned_global_table." .. v;
		end
	);
	
	contents = string.gsub(contents, "%$([%a%._]+)",
		function(v)
			return skinned_get_object_at(_table, v);
		end
	);
	
	contents = string.gsub(contents, "%$f%b[]",
		function(v)
			local n,n, fmt, str = string.find(v, "%$f%[(.-),(.+)%]");
			if not fmt or not str then
				return v;
			end
			
			local _s = "return string.format(".. fmt ..", ".. str ..");";
			local chunk, err = loadstring(_s, "skinned chunk");
			assert(chunk, "error calling chunk \"".. _s .."\" : ".. (err or "error"));
			return chunk();
		end
	);

	-- process all function calls
	contents = string.gsub(contents, "%$%b[]",
		function(v)
			-- actually interpret and call the function
			local v = string.gsub(v, "%$%[(.+)%]",
				function(v)
					local v = "return ".. v;
					local chunk, err = loadstring(v, "skinned chunk");
					assert(chunk, "error calling chunk \"".. v .."\" : ".. (err or "error"));
					return chunk();
				end
			);
			return v;
		end
	);

	-- find all chunks to execute
	contents = string.gsub(contents, "%$exec%b[]",
		function(v)
			-- actually interpret and execute all chunks
			local v = string.gsub(v, "%$exec%[(.+)%]",
				function(v)
					local chunk, err = loadstring(v, "skinned chunk");
					assert(chunk, "error calling chunk \"".. v .."\" : ".. (err or "error"));
					return chunk();
				end
			);
			return v;
		end
	);
	
	skinned_global_table = last_table;
	
	return contents;
end

