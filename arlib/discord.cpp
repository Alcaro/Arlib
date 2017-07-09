#include "discord.h"
#include <time.h>

void Discord::headers(array<string>& h)
{
	if (bot)
	{
		h.append("Authorization: Bot "+token);
		h.append("User-Agent: DiscordBot (https://github.com/Alcaro/Arlib/blob/master/arlib/discord.cpp, "
						 __DATE__ " " __TIME__ ")");
	}
	else
	{
		*(char*)0=0;
	}
}

void Discord::http(HTTP::req r, function<void(HTTP::rsp)> callback)
{
	if (ratelimit && ratelimit >= time(NULL))
	{
		HTTP::rsp fakersp;
		fakersp.success = false;
		fakersp.status = 429;
		callback(std::move(fakersp));
		return;
	}
	else ratelimit = 0;
	
	r.url = "https://discordapp.com/api" + r.url;
	headers(r.headers);
	//if (r.method == "POST" && !r.postdata) r.postdata = "{}";
	puts("request "+r.url);
	m_http.send(r);
	m_http_reqs.append(callback);
}

void Discord::http_process()
{
if(m_http_reqs)puts("HTTPACTIVE:"+tostring(m_http_reqs.size()));
	//if (!m_http_reqs) return;
	if (m_http.ready())
	{
		HTTP::rsp r = m_http.recv();
puts("remove this once rate limit behavior has been found");
puts(tostring(r.status));
for(string& h : r.headers)puts(h);
		if (r.status == 429)
		{
			size_t timer;
			fromstring(r.header("Retry-After"), timer);
			if (timer < 1000) timer = 1000;
			ratelimit = time(NULL) + (timer+999)/1000;
		}
if (!m_http_reqs) *(char*)0=0; // should never happen
		m_http_reqs[0](std::move(r));
		m_http_reqs.remove(0);
	}
}

//takes a User object, with ["id"]
void Discord::set_user_inner(JSON& json)
{
	if (json["username"])
	{
		cstring uid = json["id"];
		i_user& user = users[uid];
		user.username = json["username"];
		user.discriminator = json["discriminator"];
	}
}

//takes a Guild Member object, with ["user"] and ["roles"]
void Discord::set_user(cstring guild_id, JSON& json)
{
	cstring uid = json["user"]["id"];
	i_user& user = users[uid];
	
	set_user_inner(json["user"]);
	
	i_guild& guild = guilds[guild_id];
	guild.users.add(uid);
	user.nicks[guild_id] = json["nick"];
	
	if (json["roles"])
	{
		for (cstring r : guild.roles)
		{
			user.roles.remove(r);
		}
		for (JSON& r : json["roles"].list())
		{
			user.roles.add(r.str());
		}
	}
}

void Discord::del_user(cstring guild_id, cstring user_id)
{
	i_guild& guild = guilds[guild_id];
	i_user& user = users[user_id];
	for (cstring role_id : guild.roles)
	{
		user.roles.remove(role_id);
	}
	guild.users.remove(user_id);
	for (auto& otherguild : guilds)
	{
		if (otherguild.value.users.contains(user_id)) return;
	}
	users.remove(user_id);
}

void Discord::datelog(cstring text)
{
	time_t rawtime;
	struct tm * timeinfo;
	time(&rawtime);
	timeinfo=localtime(&rawtime);
	char out[64];
	strftime(out, 64, "[%H:%M:%S] ", timeinfo);
	puts(out+text);
}

void Discord::debug()
{
	for (auto& role : roles)
	{
		puts("role "+role.key+": "+role.value.name+" @ "+role.value.guild);
	}
	for (auto& channel : channels)
	{
		puts("chan "+channel.key+": "+channel.value.name+" @ "+channel.value.guild);
		for (cstring user : channel.value.users) puts("  member "+user);
	}
	for (auto& user : users)
	{
		puts("user "+user.key+": "+user.value.username+"#"+user.value.discriminator);
		for (auto& nick : user.value.nicks) puts("  nick @"+nick.key+" "+nick.value);
		for (cstring role : user.value.roles) puts("  role "+role);
	}
	for (auto& guild : guilds)
	{
		puts("guild "+guild.key+": "+guild.value.name);
		for (cstring user : guild.value.users) puts("  member "+user);
		for (cstring role : guild.value.roles) puts("  role "+role);
		for (cstring channel : guild.value.channels) puts("  channel "+channel);
	}
}

void Discord::connect_bot(cstring token)
{
	this->bot = true;
	this->token = token;
	connect();
}

void Discord::connect()
{
puts("DOCONNECT");
	connecting = true;
	keepalive_sent = false;
	keepalive_next = 0;
	
	if (bot) http(HTTP::req("/gateway/bot"), bind_this(&Discord::connect_cb));
	else *(char*)0=0;
}

void Discord::connect_cb(HTTP::rsp r)
{
	connecting = false;
	
	guilds_to_join = -1;
	puts(r.text());
	
	string ws_url = JSON(r.text())["url"];
	if (!ws_url) return;
	ws_url += "?v=5&encoding=json";
puts("DOCONNECT:"+ws_url);
	array<string> heads;
	headers(heads);
	m_ws.connect(ws_url, heads);
}

bool Discord::process(bool block)
{
	http_process();
	if (!m_ws)
	{
		if (!connecting) connect();
		return false;
	}
	
	if (keepalive_next && time(NULL) >= keepalive_next)
	{
		keepalive_next = time(NULL)+(keepalive_ms/1000);
		
		JSON json;
		json["op"] = 1;
		json["d"] = sequence;
		send(json);
		
		if (keepalive_sent)
		{
			m_ws.reset();
			return false;
		}
		keepalive_sent = true;
	}
	
	string msg = m_ws.recvstr(block);
	if (!msg) return false;
	
	//if (msg.length() > 1000)
	//{
	//	datelog(">> "+msg.csubstr(0,600)+"...");
	//}
	//else
	{
		datelog(">> "+msg);
	}
	
	JSON json(msg);
	if (!json) abort();
	if (json["op"]==0) // Dispatch
	{
		sequence = json["s"];
		if (json["t"] == "READY")
		{
			my_user = json["d"]["user"]["id"];
			for (JSON& g : json["d"]["guilds"].list())
			{
				guilds.get_create(g["id"]);
			}
			guilds_to_join = guilds.size();
			resume = json["d"]["session_id"];
		}
		if (json["t"] == "GUILD_CREATE")
		{
			cstring id = json["d"]["id"];
			i_guild& g = guilds[id];
			g.name = json["d"]["name"];
			for (JSON& member_j : json["d"]["members"].list())
			{
				set_user(id, member_j);
			}
			for (JSON& chan_j : json["d"]["channels"].list())
			{
				cstring cid = chan_j["id"];
				i_channel& c = channels[chan_j["id"]];
				g.channels.add(cid);
				c.guild = id;
				c.name = chan_j["name"];
				//c.topic = chan_j["topic"]; // not needed
			}
			for (JSON& role_j : json["d"]["roles"].list())
			{
				cstring rid = role_j["id"];
				i_role& r = roles[rid];
				g.roles.add(rid);
				
				r.guild = id;
				r.name = role_j["name"];
			}
			
			if (json["d"]["large"])
			{
				JSON reqfull;
				reqfull["op"] = 8; // Request Guild Members
				reqfull["d"]["guild_id"] = id;
				reqfull["d"]["query"] = "";
				reqfull["d"]["limit"] = 0;
				reqfull["why-do-i-need-this"] =
					"workaround for what appears to be a bug on your end, "
					"sometimes I get ID-only user objects without having seen that user anywhere else; "
					"are events disappearing?";
				send(reqfull);
			}
			
			guilds_to_join--;
			on_guild_enter(Guild(this, id), User(this, my_user, id));
		}
		if (json["t"] == "GUILD_MEMBERS_CHUNK")
		{
			cstring id = json["d"]["guild_id"];
			for (JSON& member_j : json["d"]["members"].list())
			{
				set_user(id, member_j);
			}
		}
		if (json["t"] == "GUILD_MEMBER_ADD")
		{
			cstring guild_id = json["d"]["guild_id"];
			set_user(guild_id, json["d"]);
			on_join(Guild(this, guild_id), User(this, json["d"]["user"]["id"], guild_id));
		}
		if (json["t"] == "GUILD_MEMBER_UPDATE" ||
		    json["t"] == "PRESENCE_UPDATE") // used for tag changes, don't like how it's documented partial/unreliable but apparently I need it
		{
			set_user(json["d"]["guild_id"], json["d"]);
		}
		if (json["t"] == "GUILD_MEMBER_REMOVE")
		{
			del_user(json["d"]["guild_id"], json["d"]["user"]["id"]);
		}
		if (json["t"] == "MESSAGE_CREATE")
		{
			string chan = json["d"]["channel_id"];
			string user = json["d"]["author"]["id"];
			string text = json["d"]["content"];
			
			set_user(channels[chan].guild, json["d"]);
			
			if (user != my_user)
			{
				on_msg(Channel(this, chan), User(this, user, channels[chan].guild), text);
			}
		}
	}
	//if (json["op"]==1) // Heartbeat (client only)
	//if (json["op"]==2) // Identify (client only)
	//if (json["op"]==3) // Status Update (only idle/in-game, not interesting)
	//if (json["op"]==4) // Voice State Update (voice unsupported)
	//if (json["op"]==5) // Voice Server Ping (voice unsupported)
	//if (json["op"]==6) // Resume (client only)
	if (json["op"]==7) // Reconnect
	{
		m_ws.reset();
	}
	//if (json["op"]==8) // Request Guild Members (client only)
	//if (json["op"]==9) // Invalid Session (resume unsupported)
	if (json["op"]==10) // Hello
	{
		keepalive_ms = json["d"]["heartbeat_interval"];
		keepalive_next = time(NULL)+(keepalive_ms/1000);
		
		if (resume)
		{
			JSON json;
			json["op"] = 6;
			json["d"]["token"] = token;
			json["d"]["session_id"] = resume;
			json["d"]["seq"] = sequence;
			send(json);
			resume = "";
		}
		else
		{
			JSON json;
			json["op"] = 2;
			json["d"]["token"] = token;
			json["d"]["compress"] = false;
			json["d"]["properties"]["os"] = "linux";
			json["d"]["properties"]["browser"] = "standalone";
			json["d"]["properties"]["device"] = "AlcaRobot (closed source)";
			json["d"]["properties"]["referrer"] = "";
			json["d"]["properties"]["referring_domain"] = "";
			json["d"]["large_threshold"] = 250;
			send(json);
		}
	}
	if (json["op"]==11) // Heartbeat ACK
	{
		keepalive_sent = false;
	}
	
	return true;
}
