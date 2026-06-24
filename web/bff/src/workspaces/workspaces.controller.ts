import { Body, Controller, Get, Param, Post, Put } from '@nestjs/common';
import { ApiTags } from '@nestjs/swagger';
import { IsOptional, IsString, Matches } from 'class-validator';
import { CurrentUser, type AuthUser } from '../common/decorators';
import { WorkspacesService } from './workspaces.service';

class CreateWorkspaceDto {
  @IsString() name!: string;
  @IsOptional() @Matches(/^\d{4}-\d{2}-\d{2}$/) today?: string;
  @IsOptional() @IsString() currency?: string;
}
class UpdateWorkspaceDto {
  @IsOptional() @IsString() name?: string;
  @IsOptional() @Matches(/^\d{4}-\d{2}-\d{2}$/) today?: string;
  @IsOptional() @IsString() currency?: string;
}

@ApiTags('workspaces')
@Controller('workspaces')
export class WorkspacesController {
  constructor(private readonly workspaces: WorkspacesService) {}

  @Get()
  list() {
    return this.workspaces.list();
  }

  @Get(':id')
  get(@Param('id') id: string) {
    return this.workspaces.get(id);
  }

  @Post()
  create(@Body() dto: CreateWorkspaceDto, @CurrentUser() user: AuthUser) {
    return this.workspaces.create({ ...dto, ownerId: user.userId });
  }

  @Put(':id')
  update(@Param('id') id: string, @Body() dto: UpdateWorkspaceDto) {
    return this.workspaces.update(id, dto);
  }
}
