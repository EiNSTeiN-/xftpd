--
-- Copyright (c) 2007, The xFTPd Project.
-- All rights reserved.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions are met:
--
--		 * Redistributions of source code must retain the above copyright
--       notice, this list of conditions and the following disclaimer.
--
--     * Redistributions in binary form must reproduce the above copyright
--       notice, this list of conditions and the following disclaimer in the
--       documentation and/or other materials provided with the distribution.
--
--     * Neither the name of the xFTPd Project nor the
--       names of its contributors may be used to endorse or promote products
--       derived from this software without specific prior written permission.
--
--     * Redistributions of this project or parts of this project in any form
--       must retain the following aknowledgment:
--       "This product includes software developed by the xFTPd Project.
--        http://www.xftpd.com/ - http://www.xftpd.org/"
--
-- THIS SOFTWARE IS PROVIDED BY THE xFTPd PROJECT ``AS IS'' AND ANY
-- EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
-- WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
-- DISCLAIMED. IN NO EVENT SHALL THE xFTPd PROJECT BE LIABLE FOR ANY
-- DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
-- (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
-- LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
-- ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
-- (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
-- SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-- Contains the underlying functions used by xFTPd's scripts

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
	
	if not ctx[0] then
		print("no iterator");
		return nil, nil;
	end
	if not ctx[1] then
		print("no totype");
		return nil, nil;
	end
	
	c = collection.iterate(state, ctx[0], ctx[1]);
	if not c then
		return nil, nil;
	end
	
	return { [0] = ctx[0]; [1] = ctx[1]; }, c;
end

-- make something like below possible:
--
--	for k, item in casted(collection, "usertype_to_cast") do
--		-- item can now be used as
--		-- a "usertype_to_cast" object type
--	end
function casted(state, totype)
	return casted_iteration, state, { [0] = collection.iterator(state); [1] = totype; };
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
	return {
		b = "\002",
		c = "\003"
	};
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
	
	assert(line, "no line");
	assert(xftpd_skin_config, "no config loaded");
	contents = config.read(xftpd_skin_config, line);
	
	skinned_global_table = _table or skintable();
	
	if not contents then
		assert(line, "line fucked up");
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
			return chunk() or "";
		end
	);

	-- process all function calls
	contents = string.gsub(contents, "%$%b[]",
		function(v)
			-- actually interpret and call the function
			v = string.gsub(v, "%$%[(.+)%]",
				function(v)
					local v = "return ".. v;
					local chunk, err = loadstring(v, "skinned chunk");
					assert(chunk, "error calling chunk \"".. v .."\" : ".. (err or "error"));
					return chunk() or "";
				end
			);
			return v;
		end
	);

	-- find all chunks to execute
	contents = string.gsub(contents, "%$exec%b[]",
		function(v)
			-- actually interpret and execute all chunks
			v = string.gsub(v, "%$exec%[(.+)%]",
				function(v)
					local chunk, err = loadstring(v, "skinned chunk");
					assert(chunk, "error calling chunk \"".. v .."\" : ".. (err or "error"));
					return chunk() or "";
				end
			);
			return v;
		end
	);
	
	skinned_global_table = last_table;
	
	return contents;
end


function hook(prefix, name, params)

	local s = string.gsub(name, " ", "_");

	if(params.irc) then
		irc.hook(irc.hooks, name, prefix .."irc_".. s, params.irc_source or IRC_SOURCE_ANY);
		
		-- create a new wrapper function
		local v = "function ".. prefix .."irc_".. s .."(msg)\n"..
			"local _ctx = { source = \"irc\"; func = irc.broadcast; param = msg.dest; msg = msg; user = getuser(msg.src); };\n"..
			"local r = ".. prefix .. s .."(_ctx, msg.args);\n"..
			"_ctx = nil;\n"..
			"return r;\n"..
		"end\n";
		
		assert(loadstring(v, "irc wrapper for \"".. name .."\""))();
		v = nil;
	end
	
	if(params.site) then
		site.hook(site.hooks, name, prefix .."site_".. s);
		
		-- create a new wrapper function
		local v = "function ".. prefix .."site_".. s .."(client, args)\n"..
			"local _ctx = { source = \"site\"; func = clients.msg; param = client; client = client; user = client.user; root = client.working_directory; };\n"..
			"local r = ".. prefix .. s .."(_ctx, args);\n"..
			"_ctx = nil;\n"..
			"return r;\n"..
		"end\n";
		
		assert(loadstring(v, "site wrapper for \"".. name .."\""))();
		v = nil;
	end
	
	return true;
end

function echo(ctx, fmt, args)
	
	assert(ctx and ctx.func and ctx.param and ctx.source, "invalid echo context");
	
	return ctx.func(ctx.param, skinned(ctx.source ..":".. fmt, args));
end

-- return the user associated with the given hostname
function getuser(hostname)

	if not hostname then
		return nil;
	end

	local k, user = nil,nil;
	for k, user in casted(users.all, "user_ctx") do
		local s = config.read(user.config, "hostname", nil);
		if s and (string.lower(s) == string.lower(hostname)) then
			return user;
		end
	end

	return nil;
end

-- this function return true if the hostname
-- is granted the specified privilege
function isgranted(hostname, privilege)
	
	if not privilege then
		return false;
	end

	if(privilege == "any") then
		return true;
	end
	
	if not hostname then
		return false;
	end

	local user = getuser(hostname);
	if not user then
		return false;
	end

	local priv = config.read(user.config, privilege .. "-privilege", nil);
	if(priv == "true") then return true; end

	return false;
end

-- read a privilege from a user
function readpriv(user, privilege)
	
	if not privilege then
		return false;
	end
	
	if(privilege == "any") then
		return true;
	end
	
	if not user then
		return false;
	end

	local priv = config.read(user.config, privilege .. "-privilege", nil);
	if(priv == "true") then return true; end

	return false;
end

