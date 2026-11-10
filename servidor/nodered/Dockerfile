# Imagen oficial de Node-RED + el driver de MongoDB que usa lib/secador.js.
FROM nodered/node-red:5.0.9
WORKDIR /data
RUN npm install --omit=dev --no-fund --no-audit mongodb@7.8.0
COPY settings.js flows.json flows_cred.json /data/
COPY lib /data/lib
WORKDIR /usr/src/node-red
ENV NODE_RED_ENABLE_PROJECTS=false
CMD ["npm", "start", "--", "--userDir", "/data", "--settings", "/data/settings.js"]
