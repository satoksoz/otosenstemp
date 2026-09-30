FROM node:22-alpine

WORKDIR /app/platform

COPY platform/package.json platform/package-lock.json ./
RUN npm ci --omit=dev

COPY platform/src/server.js ./src/server.js
COPY platform/src/store.js ./src/store.js
COPY web/ /app/web/

ENV NODE_ENV=production

CMD ["npm", "start"]